// Sonic Riders launcher for the Mac (builds the iPhone / iPad app): the macOS twin of launcher/launcher.cpp.
// It holds no game code or data. It checks what the build needs (Xcode, Metal Toolchain, Homebrew tools), runs
// tools/setup_mac.py with your disc image and optional icon picture, and walks you through installing the .ipa
// (Sideloadly or straight from the Mac), Developer Mode, trusting the developer and renewing every 7 days.
// Build: python3 tools/build_mac_launcher.py (docs/notes/ios.md "Mac launcher").
// Testing: SR_LAUNCHER_ROOT=<release folder>, SR_LAUNCHER_PAGE=0-4, SR_LAUNCHER_SHOT=<file.png> (draws the
// page off-screen, saves the picture and quits), SR_LAUNCHER_BUILD=1 (starts the build at once; with
// SR_LAUNCHER_SHOT the shot is taken when the build ends), SR_LAUNCHER_ISO=<image> (instead of the saved
// choice), SR_LAUNCHER_IPA_DIR=<dir> (instead of ~/Desktop/Sonic Riders iOS).
import AppKit
import SwiftUI
import UniformTypeIdentifiers

// ---------------------------------------------------------------- look (launcher.cpp's colours)

extension Color {
    init(_ r: Int, _ g: Int, _ b: Int) { self.init(red: Double(r) / 255, green: Double(g) / 255, blue: Double(b) / 255) }
}
let kGold = Color(242, 190, 60), kText = Color(230, 235, 245), kDim = Color(148, 158, 184)
let kGreen = Color(115, 217, 128), kRed = Color(242, 115, 102), kYellow = Color(240, 200, 90)
let kBg = Color(20, 24, 36), kSide = Color(14, 17, 27), kLine = Color(48, 56, 82), kBox = Color(28, 33, 50)

let home = FileManager.default.homeDirectoryForCurrentUser.path
let ipaDir = ProcessInfo.processInfo.environment["SR_LAUNCHER_IPA_DIR"] ?? home + "/Desktop/Sonic Riders iOS"
let ipaPath = ipaDir + "/SonicRiders.ipa"
let xcodeDev = "/Applications/Xcode.app/Contents/Developer"
let iconExts = ["png", "jpg", "jpeg", "heic", "heif", "webp", "gif", "bmp", "tif", "tiff"]

// ---------------------------------------------------------------- small helpers

func env() -> [String: String] {
    var e = ProcessInfo.processInfo.environment
    if FileManager.default.fileExists(atPath: xcodeDev) { e["DEVELOPER_DIR"] = xcodeDev }
    e["PATH"] = "/opt/homebrew/bin:/usr/local/bin:" + (e["PATH"] ?? "/usr/bin:/bin:/usr/sbin:/sbin")
    e["PYTHONUNBUFFERED"] = "1"
    return e
}

/// runs a command to the end; (exit code, output)
@discardableResult
func sh(_ args: [String], cwd: String? = nil) -> (Int32, String) {
    let p = Process(), pipe = Pipe()
    p.executableURL = URL(fileURLWithPath: args[0].hasPrefix("/") ? args[0] : "/usr/bin/env")
    p.arguments = args[0].hasPrefix("/") ? Array(args.dropFirst()) : args
    p.environment = env()
    if let cwd { p.currentDirectoryURL = URL(fileURLWithPath: cwd) }
    p.standardOutput = pipe
    p.standardError = pipe
    do { try p.run() } catch { return (-1, "") }
    let data = pipe.fileHandleForReading.readDataToEndOfFile()
    p.waitUntilExit()
    return (p.terminationStatus, String(decoding: data, as: UTF8.self))
}

func exists(_ p: String) -> Bool { FileManager.default.fileExists(atPath: p) }
func open(_ s: String) { if let u = URL(string: s) { NSWorkspace.shared.open(u) } }
func reveal(_ p: String) { NSWorkspace.shared.activateFileViewerSelecting([URL(fileURLWithPath: p)]) }
func shortPath(_ p: String) -> String { p.hasPrefix(home) ? "~" + p.dropFirst(home.count) : p }

/// the folder the app was really started from: a downloaded app opened in place runs from a random read-only copy
/// ("App Translocation") until its quarantine flag is gone
func originalURL(_ url: URL) -> URL {
    guard url.path.contains("/AppTranslocation/"),
          let h = dlopen("/System/Library/Frameworks/Security.framework/Security", RTLD_NOW),
          let f = dlsym(h, "SecTranslocateCreateOriginalPathForURL") else { return url }
    typealias Fn = @convention(c) (CFURL, UnsafeMutablePointer<Unmanaged<CFError>?>?) -> Unmanaged<CFURL>?
    let fn = unsafeBitCast(f, to: Fn.self)
    return fn(url as CFURL, nil)?.takeRetainedValue() as URL? ?? url
}

/// the release folder: SR_LAUNCHER_ROOT, else the nearest folder above the app holding tools/setup_mac.py
func findRoot() -> String? {
    if let r = ProcessInfo.processInfo.environment["SR_LAUNCHER_ROOT"] { return r }
    var u = originalURL(Bundle.main.bundleURL).deletingLastPathComponent()
    for _ in 0..<4 {
        if exists(u.path + "/tools/setup_mac.py") { return u.path }
        u = u.deletingLastPathComponent()
    }
    return nil
}

// ---------------------------------------------------------------- state

enum CheckState { case ok, missing, optional, working, unknown }

struct Check: Identifiable {
    let id: String
    var title: String
    var state: CheckState = .unknown
    var detail: String = ""
}

@MainActor
final class Model: ObservableObject {
    let root: String?
    @Published var page: Int = 0
    @Published var checks: [Check] = []
    @Published var team = ""
    @Published var iso: String = ProcessInfo.processInfo.environment["SR_LAUNCHER_ISO"] ?? UserDefaults.standard.string(forKey: "iso") ?? ""
    @Published var icon: String = UserDefaults.standard.string(forKey: "icon") ?? ""
    @Published var iconPreview: NSImage?
    @Published var signed: Bool = UserDefaults.standard.bool(forKey: "signed")
    @Published var log: [String] = []
    @Published var step = 0
    @Published var stepText = ""
    @Published var ninja = 0.0
    @Published var task = ""            // what is running ("" = nothing)
    @Published var status = ""
    @Published var failed = false
    @Published var ipaDate: Date?
    @Published var installedAt: Date? = UserDefaults.standard.object(forKey: "installedAt") as? Date
    @Published var route: String = UserDefaults.standard.string(forKey: "route") ?? "sideloadly"
    @Published var guide: Int = UserDefaults.standard.integer(forKey: "guide")
    @Published var devices: [(id: String, name: String)] = []
    @Published var device = ""
    var proc: Process?
    var onTaskEnd: ((Int32) -> Void)?

    init() {
        root = findRoot()
        if let root {  // nothing in the folder should ask Gatekeeper again (decomp-toolkit, scripts)
            DispatchQueue.global().async { sh(["/usr/bin/xattr", "-dr", "com.apple.quarantine", root]) }
        }
        refreshIpa()
        if !icon.isEmpty { makePreview() }
        recheck()
    }

    /// the default app icon (setup/icon.png, cedry2kio's fan art), shown while no picture is chosen
    lazy var defaultIconImage: NSImage? = root.flatMap { NSImage(contentsOfFile: $0 + "/setup/icon.png") }
    var folderOK: Bool { guard let root else { return false }; return !root.contains(" ") && root.allSatisfy(\.isASCII) }
    var ready: Bool { checks.allSatisfy { $0.state == .ok || $0.state == .optional } && !checks.isEmpty }
    var busy: Bool { !task.isEmpty }
    var version: String {
        guard let root, let v = try? String(contentsOfFile: root + "/VERSION", encoding: .utf8) else { return "dev" }
        return v.trimmingCharacters(in: .whitespacesAndNewlines)
    }
    var expires: Date? { installedAt.map { $0.addingTimeInterval(7 * 86400) } }

    func refreshIpa() {
        ipaDate = (try? FileManager.default.attributesOfItem(atPath: ipaPath))?[.modificationDate] as? Date
    }

    func recheck() {
        let fOK = folderOK, r = root
        DispatchQueue.global().async {
            var c: [Check] = []
            c.append(Check(id: "folder", title: "Release folder", state: fOK ? .ok : .missing,
                           detail: r == nil ? "Keep this launcher inside the extracted SonicRiders folder."
                                 : fOK ? shortPath(r!) : "The path has spaces or non-English letters: \(shortPath(r!))"))
            let mac = sh(["/usr/bin/uname", "-m"]).1.contains("arm64")
            c.append(Check(id: "mac", title: "Apple silicon Mac", state: mac ? .ok : .missing,
                           detail: mac ? "M1 or newer" : "The build needs an M1 or newer Mac."))
            let xc = exists(xcodeDev)
            c.append(Check(id: "xcode", title: "Xcode", state: xc ? .ok : .missing,
                           detail: xc ? sh(["xcodebuild", "-version"]).1.replacingOccurrences(of: "\n", with: " ")
                                      : "Free in the App Store (about 10 GB). Install it, then come back."))
            if xc {
                let first = sh(["xcodebuild", "-checkFirstLaunchStatus"]).0 == 0
                c.append(Check(id: "first", title: "Xcode set up", state: first ? .ok : .missing,
                               detail: first ? "License accepted, components installed"
                                             : "Open Xcode once: agree to the license, tick iOS, wait until it finishes."))
                let sdk = !sh(["xcrun", "--sdk", "iphoneos", "--show-sdk-path"]).1.isEmpty && first
                c.append(Check(id: "sdk", title: "iOS platform", state: sdk ? .ok : .missing,
                               detail: sdk ? "iOS SDK found" : "Xcode's iOS platform (about 8 GB download)."))
                let metal = sh(["xcrun", "-sdk", "iphoneos", "metal", "--version"]).0 == 0
                c.append(Check(id: "metal", title: "Metal Toolchain", state: metal ? .ok : .missing,
                               detail: metal ? "Found" : "An Xcode component for the game's shaders (about 700 MB)."))
            }
            let brew = ["/opt/homebrew/bin/brew", "/usr/local/bin/brew"].first(where: exists)
            c.append(Check(id: "brew", title: "Homebrew", state: brew != nil ? .ok : .missing,
                           detail: brew != nil ? "Found" : "A free installer for build tools (brew.sh)."))
            let tools = ["ninja", "glslc"].allSatisfy { t in ["/opt/homebrew/bin/", "/usr/local/bin/"].contains { exists($0 + t) } }
            c.append(Check(id: "tools", title: "ninja and shaderc", state: tools ? .ok : .missing,
                           detail: tools ? "Found" : "Two small build tools from Homebrew."))
            var team = ""
            if let r, xc {
                team = sh(["python3", "-I", "-c", "import sys; sys.path.insert(0, 'tools'); import ios_build; print(ios_build.find_team())"],
                          cwd: r).1.trimmingCharacters(in: .whitespacesAndNewlines)
                if team.count != 10 { team = "" }
            }
            c.append(Check(id: "appleid", title: "Apple ID in Xcode (optional)", state: team.isEmpty ? .optional : .ok,
                           detail: team.isEmpty ? "Not needed: Sideloadly signs the app with your Apple ID."
                                                : "Team \(team): the app can also be installed straight from this Mac."))
            DispatchQueue.main.async {
                self.checks = c
                self.team = team
                if team.isEmpty { self.signed = false }
            }
        }
    }

    // ------------------------------------------------ running commands, output into the log box

    func run(_ what: String, _ args: [String], then: ((Int32) -> Void)? = nil) {
        guard !busy else { return }
        let p = Process(), pipe = Pipe()
        p.executableURL = URL(fileURLWithPath: "/usr/bin/env")
        p.arguments = args
        p.environment = env()
        if let root { p.currentDirectoryURL = URL(fileURLWithPath: root) }
        p.standardOutput = pipe
        p.standardError = pipe
        p.standardInput = FileHandle.nullDevice
        task = what
        status = what + "..."
        failed = false
        log.append("> " + what)
        onTaskEnd = then
        var partial = ""
        pipe.fileHandleForReading.readabilityHandler = { h in
            let d = h.availableData
            if d.isEmpty { return }
            let s = String(decoding: d, as: UTF8.self).replacingOccurrences(of: "\r", with: "\n")
            DispatchQueue.main.async {
                partial += s
                var lines = partial.components(separatedBy: "\n")
                partial = lines.removeLast()
                for l in lines where !l.isEmpty { self.line(l) }
            }
        }
        p.terminationHandler = { p in
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.3) {
                pipe.fileHandleForReading.readabilityHandler = nil
                let rc = p.terminationStatus
                self.task = ""
                self.proc = nil
                if rc != 0 && !self.failed { self.failed = true; self.status = "\(what): failed (exit code \(rc))" }
                else if rc == 0 { self.status = "\(what): done" }
                self.onTaskEnd?(rc)
                self.recheck()
                self.refreshIpa()
            }
        }
        do { try p.run(); proc = p } catch { task = ""; status = "could not start \(args.first ?? "")"; failed = true }
    }

    func line(_ l: String) {
        let t = l.replacingOccurrences(of: "\u{1b}\\[[0-9;]*m", with: "", options: .regularExpression)
        log.append(t)
        if log.count > 600 { log.removeFirst(log.count - 600) }
        if let m = t.range(of: #"^== (\d)/7\s+(.*)$"#, options: .regularExpression) {
            let parts = t[m].dropFirst(3).split(separator: "/", maxSplits: 1)
            step = Int(parts[0]) ?? step
            stepText = String(t.dropFirst(8)).trimmingCharacters(in: .whitespaces)
            ninja = 0
        } else if let m = t.range(of: #"\[(\d+)/(\d+)\]"#, options: .regularExpression) {
            let n = t[m].dropFirst().dropLast().split(separator: "/").compactMap { Double($0) }
            if n.count == 2 && n[1] > 0 { ninja = n[0] / n[1] }
        } else if t.hasPrefix("SETUP FAILED") {
            failed = true
            status = String(t.dropFirst(14))
        }
    }

    func stop() { proc?.terminate() }

    // ------------------------------------------------ actions

    func build() {
        guard root != nil, !iso.isEmpty else { return }
        page = 1
        log = []
        step = 0
        stepText = "Starting"
        var a = ["/usr/bin/python3", "-u", "tools/setup_mac.py", "--yes", "--install", "no", "--iso", iso,
                 "--icon", icon.isEmpty ? "default" : icon, "--ipa-dir", ipaDir]
        if !signed { a.append("--unsigned") }
        UserDefaults.standard.set(signed, forKey: "signed")
        run("Building the app", a) { rc in
            if rc == 0 {
                self.step = 8
                self.status = "Built: \(shortPath(ipaPath))"
                self.route = self.signed ? "mac" : "sideloadly"
                UserDefaults.standard.set(self.route, forKey: "route")
            }
            NSApp.requestUserAttention(.informationalRequest)
        }
    }

    func pickIso() {
        let p = NSOpenPanel()
        p.message = "Select your Sonic Riders (USA) disc image (GXEE8P, revision 0, .iso)"
        p.allowedContentTypes = [UTType(filenameExtension: "iso") ?? .data, .data]
        if p.runModal() == .OK, let u = p.url {
            iso = u.path
            UserDefaults.standard.set(iso, forKey: "iso")
        }
    }

    func pickIcon() {
        let p = NSOpenPanel()
        p.message = "Pick a picture for the app icon (the centre square is used; transparent parts become blue)"
        p.allowedContentTypes = iconExts.compactMap { UTType(filenameExtension: $0) }
        if p.runModal() == .OK, let u = p.url {
            icon = u.path
            UserDefaults.standard.set(icon, forKey: "icon")
            makePreview()
        }
    }

    func defaultIcon() {
        icon = ""
        iconPreview = nil
        UserDefaults.standard.removeObject(forKey: "icon")
    }

    /// the icon exactly as the build makes it (tools/ios_build.py icon: sips centre square + flattened)
    func makePreview() {
        guard let root, exists(icon) else { iconPreview = nil; return }
        let src = icon
        let out = NSTemporaryDirectory() + "sr_icon_preview.png"
        DispatchQueue.global().async {
            let rc = sh(["/usr/bin/python3", "-I", "tools/ios_build.py", "icon", src, out], cwd: root).0
            let img = rc == 0 ? NSImage(contentsOfFile: out) : nil
            DispatchQueue.main.async {
                self.iconPreview = img ?? NSImage(contentsOfFile: src)
                if rc != 0 { self.status = "Preview needs Xcode; the picture is converted when the app is built" }
            }
        }
    }

    func markInstalled() {
        installedAt = Date()
        UserDefaults.standard.set(installedAt, forKey: "installedAt")
    }

    func setGuide(_ g: Int) {
        guide = g
        UserDefaults.standard.set(g, forKey: "guide")
    }

    func scanDevices() {
        guard let root else { return }
        DispatchQueue.global().async {
            let out = sh(["/usr/bin/python3", "-I", "tools/ios_run.py", "devices"], cwd: root).1
            let devs: [(id: String, name: String)] = out.split(separator: "\n").compactMap { l in
                let f = l.split(separator: " ", omittingEmptySubsequences: true)
                guard f.count >= 2, f[0].count >= 20, l.contains("paired") else { return nil }
                return (String(f[0]), String(f[1]))
            }
            DispatchQueue.main.async {
                self.devices = devs
                if !devs.contains(where: { $0.id == self.device }) { self.device = devs.first?.id ?? "" }
            }
        }
    }

    func installFromMac(copyDisc: Bool) {
        guard !device.isEmpty else { return }
        let dev = device
        run("Installing on the device", ["/usr/bin/python3", "-u", "tools/ios_run.py", "--device", dev, "install"]) { rc in
            guard rc == 0 else { return }
            self.markInstalled()
            if copyDisc && !self.iso.isEmpty {
                self.run("Copying the disc image to the device",
                         ["/usr/bin/python3", "-u", "tools/ios_run.py", "--device", dev, "push-iso", self.iso])
            }
        }
    }

    /// Terminal runs Homebrew's official installer (it asks for your Mac password)
    func installBrew() {
        let f = NSTemporaryDirectory() + "Install Homebrew.command"
        let s = "#!/bin/bash\necho 'Installing Homebrew (brew.sh). It asks for your Mac password; nothing shows while you type.'\n" +
            "/bin/bash -c \"$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)\"\n" +
            "echo; echo 'Done. Go back to the Sonic Riders launcher and click Check again.'\n"
        try? s.write(toFile: f, atomically: true, encoding: .utf8)
        chmod(f, 0o755)
        NSWorkspace.shared.open(URL(fileURLWithPath: f))
    }

    /// a calendar event 6 days after the install (Calendar asks before adding it)
    func addReminder() {
        guard let exp = expires else { return }
        let fmt = DateFormatter()
        fmt.dateFormat = "yyyyMMdd'T'HHmmss"
        let start = exp.addingTimeInterval(-86400)
        let ics = """
        BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//SonicRidersLauncher//EN\r\nBEGIN:VEVENT\r\n\
        UID:\(UUID().uuidString)\r\nDTSTAMP:\(fmt.string(from: Date()))\r\nDTSTART:\(fmt.string(from: start))\r\n\
        DTEND:\(fmt.string(from: start.addingTimeInterval(1800)))\r\nSUMMARY:Renew Sonic Riders on iPhone/iPad\r\n\
        DESCRIPTION:Open the Sonic Riders launcher on your Mac > Renew. The app stops opening tomorrow.\r\n\
        BEGIN:VALARM\r\nTRIGGER:PT0M\r\nACTION:DISPLAY\r\nDESCRIPTION:Renew Sonic Riders\r\nEND:VALARM\r\n\
        END:VEVENT\r\nEND:VCALENDAR\r\n
        """
        let f = NSTemporaryDirectory() + "Renew Sonic Riders.ics"
        try? ics.write(toFile: f, atomically: true, encoding: .utf8)
        NSWorkspace.shared.open(URL(fileURLWithPath: f))
    }

    /// the folder to ~/SonicRiders (no spaces), then the launcher starts again from there
    func moveFolder() {
        guard let root else { return }
        var dst = home + "/SonicRiders", n = 2
        while exists(dst) { dst = home + "/SonicRiders\(n)"; n += 1 }
        do {
            try FileManager.default.moveItem(atPath: root, toPath: dst)
            let app = dst + "/" + originalURL(Bundle.main.bundleURL).lastPathComponent
            let cfg = NSWorkspace.OpenConfiguration()
            cfg.createsNewApplicationInstance = true
            NSWorkspace.shared.openApplication(at: URL(fileURLWithPath: app), configuration: cfg) { _, _ in
                DispatchQueue.main.async { NSApp.terminate(nil) }
            }
        } catch {
            status = "Could not move the folder: \(error.localizedDescription)"
            failed = true
        }
    }
}

// ---------------------------------------------------------------- views

struct Section_: View {
    let t: String
    var body: some View {
        Text(t.uppercased()).font(.system(size: 12, weight: .bold)).tracking(1.2).foregroundColor(kGold).padding(.bottom, 2)
    }
}

struct Btn: View {
    let t: String
    var primary = false
    var enabled = true
    let action: () -> Void
    var body: some View {
        Button(action: action) {
            Text(t).font(.system(size: primary ? 15 : 13, weight: primary ? .heavy : .semibold))
                .foregroundColor(primary ? Color(20, 20, 28) : kText)
                .padding(.horizontal, primary ? 22 : 12).padding(.vertical, primary ? 11 : 6)
                .background(RoundedRectangle(cornerRadius: 4).fill(primary ? kGold : Color(36, 43, 66)))
                .opacity(enabled ? 1 : 0.4)
        }.buttonStyle(.plain).disabled(!enabled)
    }
}

struct Para: View {
    let t: String
    var color = kText
    init(_ t: String, _ color: Color = kText) { self.t = t; self.color = color }
    var body: some View {
        Text(.init(t)).font(.system(size: 13)).foregroundColor(color).fixedSize(horizontal: false, vertical: true)
            .tint(kGold)
    }
}

struct Mark: View {
    let s: CheckState
    var body: some View {
        switch s {
        case .ok: Text("✓").foregroundColor(kGreen)
        case .missing: Text("✗").foregroundColor(kRed)
        case .optional: Text("–").foregroundColor(kDim)
        case .working, .unknown: Text("…").foregroundColor(kDim)
        }
    }
}

struct LogBox: View {
    @ObservedObject var m: Model
    var body: some View {
        ScrollViewReader { sv in
            ScrollView {
                LazyVStack(alignment: .leading, spacing: 1) {
                    ForEach(Array(m.log.enumerated()), id: \.offset) { i, l in
                        Text(l).font(.system(size: 11, design: .monospaced))
                            .foregroundColor(l.hasPrefix("SETUP FAILED") ? kRed : l.hasPrefix("==") ? kGold : kDim)
                            .textSelection(.enabled).id(i)
                    }
                }.padding(8).frame(maxWidth: .infinity, alignment: .leading)
            }
            .background(RoundedRectangle(cornerRadius: 4).fill(kSide))
            .onChange(of: m.log.count) { n in sv.scrollTo(n - 1, anchor: .bottom) }
        }
    }
}

// --- page 0: get ready

struct ReadyPage: View {
    @ObservedObject var m: Model
    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Section_(t: "Get ready")
            Para("Everything the build needs, in order. Fix the first red line, then click **Check again**. "
                 + "Nothing here costs money; a free Apple ID is enough.", kDim)
            ScrollView {
                VStack(alignment: .leading, spacing: 8) {
                    ForEach(m.checks) { c in row(c) }
                }
            }
            HStack(spacing: 10) {
                Btn(t: "Check again", enabled: !m.busy) { m.recheck() }
                if m.busy { ProgressView().controlSize(.small); Text(m.task).foregroundColor(kDim).font(.system(size: 12)) }
            }
            if !m.log.isEmpty && m.page == 0 { LogBox(m: m).frame(height: 110) }
        }
    }

    @ViewBuilder func row(_ c: Check) -> some View {
        HStack(alignment: .top, spacing: 10) {
            Mark(s: c.state).font(.system(size: 15, weight: .bold)).frame(width: 18)
            VStack(alignment: .leading, spacing: 3) {
                Text(c.title).font(.system(size: 14, weight: .semibold)).foregroundColor(kText)
                Text(c.detail).font(.system(size: 12)).foregroundColor(kDim).fixedSize(horizontal: false, vertical: true)
                if c.state == .missing { fix(c.id).padding(.top, 3) }
                if c.id == "appleid" && c.state == .optional {
                    Para("To install from this Mac instead: Xcode > Settings > Accounts > + > Apple ID.", kDim)
                }
            }
            Spacer()
        }
        .padding(10).background(RoundedRectangle(cornerRadius: 5).fill(c.state == .missing ? Color(40, 30, 40) : kBox))
    }

    @ViewBuilder func fix(_ id: String) -> some View {
        switch id {
        case "folder":
            HStack { Btn(t: "Move it to ~/SonicRiders", enabled: !m.busy && m.root != nil) { m.moveFolder() } }
        case "xcode":
            VStack(alignment: .leading, spacing: 4) {
                Para("1. Click the button, then **Get** in the App Store (sign in with your Apple ID if asked).\n"
                     + "2. Wait until it has installed (it is big: 30-60 minutes on most connections).\n"
                     + "3. Open Xcode once, agree, and when it asks for platforms tick **iOS**.", kDim)
                Btn(t: "Open Xcode in the App Store") { open("macappstore://apps.apple.com/app/xcode/id497799835") }
            }
        case "first":
            VStack(alignment: .leading, spacing: 4) {
                Para("Xcode opens. Click **Agree** (your Mac password), tick **iOS** if it asks, and wait until it "
                     + "finishes. Then quit Xcode and click Check again.", kDim)
                Btn(t: "Open Xcode") { NSWorkspace.shared.open(URL(fileURLWithPath: "/Applications/Xcode.app")) }
            }
        case "sdk":
            Btn(t: "Download the iOS platform", enabled: !m.busy) {
                m.run("Downloading Xcode's iOS platform", ["xcodebuild", "-downloadPlatform", "iOS"])
            }
        case "metal":
            Btn(t: "Download the Metal Toolchain", enabled: !m.busy) {
                m.run("Downloading the Metal Toolchain", ["xcodebuild", "-downloadComponent", "MetalToolchain"])
            }
        case "brew":
            VStack(alignment: .leading, spacing: 4) {
                Para("A Terminal window runs Homebrew's own installer. Type your Mac password when it asks (nothing "
                     + "shows while you type) and press Return when it says so. Close it when it says Done.", kDim)
                HStack {
                    Btn(t: "Install Homebrew") { m.installBrew() }
                    Btn(t: "What is Homebrew?") { open("https://brew.sh") }
                }
            }
        case "tools":
            Btn(t: "Install ninja and shaderc", enabled: !m.busy && m.checks.contains { $0.id == "brew" && $0.state == .ok }) {
                let brew = exists("/opt/homebrew/bin/brew") ? "/opt/homebrew/bin/brew" : "/usr/local/bin/brew"
                m.run("Installing ninja and shaderc", [brew, "install", "ninja", "shaderc"])
            }
        default:
            EmptyView()
        }
    }
}

// --- page 1: build

struct BuildPage: View {
    @ObservedObject var m: Model
    var body: some View {
        HStack(alignment: .top, spacing: 28) {
            VStack(alignment: .leading, spacing: 10) {
                Section_(t: "Your disc image")
                Para("Sonic Riders (USA), GXEE8P revision 0, a full .iso (1.4 GB). It never leaves your Mac except "
                     + "to your own iPhone/iPad.", kDim)
                HStack {
                    Btn(t: "Select disc...", enabled: !m.busy) { m.pickIso() }
                    Text(m.iso.isEmpty ? "no disc image selected" : (m.iso as NSString).lastPathComponent)
                        .font(.system(size: 12)).foregroundColor(m.iso.isEmpty || !exists(m.iso) ? kRed : kText).lineLimit(1)
                }
                Section_(t: "App icon").padding(.top, 8)
                HStack(alignment: .top, spacing: 14) {
                    Group {
                        if let img = m.iconPreview ?? m.defaultIconImage {
                            Image(nsImage: img).resizable().interpolation(.high)
                        } else {
                            ZStack {
                                Color(11, 61, 145)
                                Text("default\nicon").font(.system(size: 11)).multilineTextAlignment(.center).foregroundColor(kText)
                            }
                        }
                    }.frame(width: 76, height: 76).clipShape(RoundedRectangle(cornerRadius: 17, style: .continuous))
                    VStack(alignment: .leading, spacing: 6) {
                        Para("Any picture: PNG, JPG, HEIC, WEBP, GIF, BMP or TIFF. The centre square is used; "
                             + "transparent parts become blue (iOS icons can't be see-through).", kDim)
                        HStack {
                            Btn(t: "Choose picture...", enabled: !m.busy) { m.pickIcon() }
                            if !m.icon.isEmpty { Btn(t: "Use the default", enabled: !m.busy) { m.defaultIcon() } }
                        }
                    }
                }
                Section_(t: "Signing").padding(.top, 8)
                Picker("", selection: $m.signed) {
                    Text("Unsigned .ipa, installed with Sideloadly (recommended)").tag(false)
                    Text("Signed with my Apple ID in Xcode, installed from this Mac").tag(true)
                }.pickerStyle(.radioGroup).labelsHidden().disabled(m.team.isEmpty || m.busy)
                    .foregroundColor(kText).font(.system(size: 12))
                if m.team.isEmpty { Para("(Signed needs an Apple ID in Xcode; see Get ready.)", kDim) }
                Spacer()
            }.frame(width: 330)
            VStack(alignment: .leading, spacing: 8) {
                Section_(t: m.busy && m.task.hasPrefix("Building") ? "Building" : m.failed ? "Build failed" :
                            m.step == 8 ? "Built" : "Build")
                if m.step > 0 && m.step <= 8 {
                    ProgressView(value: min(1, (Double(min(m.step, 8)) - 1 + (m.step == 6 ? m.ninja : 0)) / 7))
                        .tint(m.failed ? kRed : kGold)
                    Text(m.step == 8 ? "Done: \(shortPath(ipaPath))" : "Step \(m.step) of 7: \(m.stepText)")
                        .font(.system(size: 12)).foregroundColor(m.failed ? kRed : kText)
                } else {
                    Para("The first build takes 15-30 minutes (it downloads about 300 MB of tools once). "
                         + "Rebuilds are quicker. You can keep using your Mac meanwhile.", kDim)
                    if let d = m.ipaDate {
                        Para("Last build: \(d.formatted(date: .abbreviated, time: .shortened)) - \(shortPath(ipaPath))", kGreen)
                    }
                }
                LogBox(m: m)
            }
        }
    }
}

// --- page 2: install (step by step)

struct GuideStep {
    let title: String
    let text: String
}

struct InstallPage: View {
    @ObservedObject var m: Model
    let sideSteps = [
        GuideStep(title: "Get Sideloadly", text: "Sideloadly is free. It signs the app with your Apple ID and puts it on the device. "
                  + "Download the macOS version, open the .dmg and drag Sideloadly into Applications. The first time you open it, "
                  + "macOS may ask you to allow it in **System Settings > Privacy & Security > Open Anyway**, the same as this launcher."),
        GuideStep(title: "Connect your iPhone or iPad", text: "Plug it into the Mac with a cable (a data cable; some are charge-only). "
                  + "Unlock it and tap **Trust** when it asks, then enter its passcode."),
        GuideStep(title: "Install with Sideloadly", text: "Open Sideloadly. Drag **SonicRiders.ipa** onto it (or click the button below), check that "
                  + "your device is picked at the top, type your Apple ID email, and click **Start**. Type your Apple ID password, "
                  + "and the 6-digit code if your other devices show one. Leave **Use automatic bundle ID** ticked. "
                  + "When it says Done, the app is on the device."),
        GuideStep(title: "Turn on Developer Mode (iOS 16 or newer)", text: "On the device: **Settings > Privacy & Security**, scroll to the "
                  + "bottom, **Developer Mode**, switch it on, tap **Restart**. After the restart tap **Turn On** and enter the passcode. "
                  + "(The switch only shows up after a sideloaded app has been installed once.)"),
        GuideStep(title: "Trust your Apple ID on the device", text: "On the device: **Settings > General > VPN & Device Management**, tap your "
                  + "Apple ID under Developer App, tap **Trust** and confirm. Without this iOS says \"Untrusted Developer\"."),
        GuideStep(title: "Copy your disc image to the app", text: "In Finder's sidebar click your device, then the **Files** tab. Drag your disc "
                  + "image onto **Sonic Riders** and wait until the copy has finished (a minute or two) before opening the game. "
                  + "Finder says \"The required file cannot be found\"? Click another place in the sidebar, then the device again, and "
                  + "drag it again. Only needed once: renewing keeps the disc image and your saves."),
        GuideStep(title: "Play", text: "Open Sonic Riders on the device. Touch controls are on screen; a controller works too. The round "
                  + "button at the top left opens the settings.\n\nA free Apple ID keeps the app working for **7 days**. Click "
                  + "**I installed it today** so the launcher can remind you, and see the Renew page."),
    ]

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack {
                Section_(t: m.route == "mac" ? "Install from this Mac" : "Install with Sideloadly")
                Spacer()
                if !m.team.isEmpty {
                    Picker("", selection: $m.route) {
                        Text("Sideloadly").tag("sideloadly")
                        Text("From this Mac").tag("mac")
                    }.pickerStyle(.segmented).labelsHidden().frame(width: 220)
                }
            }
            if m.ipaDate == nil {
                Para("Build the app first (Build page).", kRed)
            }
            if m.route == "mac" { macRoute } else { sideRoute }
        }
    }

    var sideRoute: some View {
        let i = min(max(m.guide, 0), sideSteps.count - 1)
        return VStack(alignment: .leading, spacing: 12) {
            HStack(spacing: 6) {
                ForEach(0..<sideSteps.count, id: \.self) { k in
                    Button { m.setGuide(k) } label: {
                        Text("\(k + 1)").font(.system(size: 12, weight: .bold))
                            .frame(width: 26, height: 26)
                            .background(Circle().fill(k == i ? kGold : k < i ? Color(50, 90, 60) : kBox))
                            .foregroundColor(k == i ? Color(20, 20, 28) : kText)
                    }.buttonStyle(.plain)
                }
            }
            Text("Step \(i + 1) of \(sideSteps.count): \(sideSteps[i].title)").font(.system(size: 17, weight: .bold)).foregroundColor(kText)
            Para(sideSteps[i].text)
            HStack(spacing: 8) { stepButtons(i) }
            Spacer()
            HStack {
                Btn(t: "Back", enabled: i > 0) { m.setGuide(i - 1) }
                Spacer()
                if i < sideSteps.count - 1 { Btn(t: "Done, next step") { m.setGuide(i + 1) } }
            }
        }
    }

    @ViewBuilder func stepButtons(_ i: Int) -> some View {
        switch i {
        case 0:
            Btn(t: "Open sideloadly.io") { open("https://sideloadly.io") }
            if exists("/Applications/Sideloadly.app") { Text("✓ Sideloadly is installed").foregroundColor(kGreen).font(.system(size: 12)) }
        case 2:
            Btn(t: "Open SonicRiders.ipa in Sideloadly", enabled: m.ipaDate != nil && exists("/Applications/Sideloadly.app")) {
                NSWorkspace.shared.open([URL(fileURLWithPath: ipaPath)], withApplicationAt: URL(fileURLWithPath: "/Applications/Sideloadly.app"),
                                        configuration: NSWorkspace.OpenConfiguration())
            }
            Btn(t: "Show the .ipa in Finder", enabled: m.ipaDate != nil) { reveal(ipaPath) }
        case 5:
            Btn(t: "Open Finder") { NSWorkspace.shared.open(URL(fileURLWithPath: home)) }
            Btn(t: "Show my disc image", enabled: exists(m.iso)) { reveal(m.iso) }
        case 6:
            Btn(t: "I installed it today", primary: false) { m.markInstalled(); m.page = 3 }
        default:
            EmptyView()
        }
    }

    var macRoute: some View {
        VStack(alignment: .leading, spacing: 10) {
            Para("The app was signed with your Apple ID in Xcode; the launcher installs it over the cable.\n"
                 + "1. Connect the iPhone/iPad with a cable, unlock it, tap **Trust**.\n"
                 + "2. Developer Mode on: **Settings > Privacy & Security > Developer Mode** (restart, Turn On).\n"
                 + "3. Click **Find devices**, pick yours, then **Install**. The first time also copy the disc image.\n"
                 + "4. First start on the device: **Settings > General > VPN & Device Management** > your Apple ID > **Trust**.")
            HStack(spacing: 8) {
                Btn(t: "Find devices", enabled: !m.busy) { m.scanDevices() }
                Picker("", selection: $m.device) {
                    if m.devices.isEmpty { Text("no device found yet").tag("") }
                    ForEach(m.devices, id: \.id) { d in Text(d.name).tag(d.id) }
                }.labelsHidden().frame(width: 220)
            }
            HStack(spacing: 8) {
                Btn(t: "Install + copy disc image", enabled: !m.busy && !m.device.isEmpty && m.ipaDate != nil) { m.installFromMac(copyDisc: true) }
                Btn(t: "Install only (renewing)", enabled: !m.busy && !m.device.isEmpty && m.ipaDate != nil) { m.installFromMac(copyDisc: false) }
            }
            LogBox(m: m)
        }
    }
}

// --- page 3: renew every 7 days

struct RenewPage: View {
    @ObservedObject var m: Model
    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Section_(t: "Renew every 7 days")
            if let exp = m.expires {
                let days = Int(ceil(exp.timeIntervalSinceNow / 86400))
                Text(days > 0 ? "Works until about \(exp.formatted(date: .complete, time: .shortened)) (\(days) day\(days == 1 ? "" : "s") left)"
                              : "Probably expired on \(exp.formatted(date: .abbreviated, time: .shortened)): renew it now")
                    .font(.system(size: 16, weight: .bold)).foregroundColor(days > 2 ? kGreen : days > 0 ? kYellow : kRed)
                Text("Installed or renewed: \(m.installedAt!.formatted(date: .abbreviated, time: .shortened))").font(.system(size: 12)).foregroundColor(kDim)
            } else {
                Para("No install date yet: click **I renewed / installed it today** after installing.", kDim)
            }
            Para("With a free Apple ID, iOS stops opening the app 7 days after it was installed. Renewing puts the "
                 + "same app back for 7 more days; **your saves and the disc image stay on the device**. You don't need to "
                 + "build again: the same SonicRiders.ipa works.")
            Para(m.route == "mac"
                 ? "**You installed from this Mac.** Connect the device, go to **Install**, click **Find devices** and **Install only "
                   + "(renewing)**. (With Sideloadly instead: untick Advanced Options > \"Use automatic bundle ID\", or you get a "
                   + "second, empty Sonic Riders.)"
                 : "**You installed with Sideloadly.** Connect the device, open Sideloadly, drag in the same SonicRiders.ipa with the "
                   + "**same Apple ID and settings** (\"Use automatic bundle ID\" left ticked) and click Start. A different way "
                   + "makes a second, empty Sonic Riders.")
            HStack(spacing: 8) {
                if m.route == "mac" { Btn(t: "Go to Install") { m.page = 2 } }
                else {
                    Btn(t: "Open .ipa in Sideloadly", enabled: m.ipaDate != nil && exists("/Applications/Sideloadly.app")) {
                        NSWorkspace.shared.open([URL(fileURLWithPath: ipaPath)], withApplicationAt: URL(fileURLWithPath: "/Applications/Sideloadly.app"),
                                                configuration: NSWorkspace.OpenConfiguration())
                    }
                }
                Btn(t: "I renewed / installed it today") { m.markInstalled() }
                Btn(t: "Add a Calendar reminder", enabled: m.expires != nil) { m.addReminder() }
            }
            Para("Free Apple IDs may have up to 3 sideloaded apps per device: remove one if Sideloadly says the maximum is reached.", kDim)
            if exists(ipaDir + "/How to renew (every 7 days).txt") {
                Btn(t: "Open \"How to renew\" notes") { NSWorkspace.shared.open(URL(fileURLWithPath: ipaDir + "/How to renew (every 7 days).txt")) }
            }
            Spacer()
        }
    }
}

// --- page 4: about

struct AboutPage: View {
    @ObservedObject var m: Model
    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            Section_(t: "About")
            Para("Sonic Riders native port \(m.version): an unofficial fan project, not affiliated with SEGA or Nintendo. "
                 + "The app is built on your Mac from your own disc image. Never share the .ipa or your disc image: they contain the game.")
            if let r = m.root { Para("Folder: \(shortPath(r))", kDim) }
            HStack(spacing: 8) {
                Btn(t: "Project page") { open("https://github.com/teclilla18/sonic-riders-recomp") }
                Btn(t: "Report a problem") { open("https://github.com/teclilla18/sonic-riders-recomp/issues") }
                if let r = m.root, exists(r + "/setup.log") { Btn(t: "Show setup.log") { reveal(r + "/setup.log") } }
                if let r = m.root, exists(r + "/START HERE (Mac).html") {
                    Btn(t: "First-start help") { NSWorkspace.shared.open(URL(fileURLWithPath: r + "/START HERE (Mac).html")) }
                }
            }
            Para("If something fails, attach setup.log (from this folder) to the report; never attach game files.", kDim)
            Spacer()
        }
    }
}

// --- the window

struct Banner: View {
    let root: String?
    var body: some View {
        ZStack(alignment: .bottomLeading) {
            if let img = Bundle.main.image(forResource: "banner") ?? root.flatMap({ NSImage(contentsOfFile: $0 + "/launcher/banner.jpg") }) {
                Image(nsImage: img).resizable().aspectRatio(contentMode: .fill).frame(height: 150).clipped()
            } else {
                LinearGradient(colors: [Color(24, 58, 140), Color(12, 22, 60)], startPoint: .topLeading, endPoint: .bottomTrailing)
            }
            LinearGradient(colors: [Color(10, 12, 20).opacity(0.92), .clear], startPoint: .leading, endPoint: .init(x: 0.62, y: 0.5))
            VStack(alignment: .leading, spacing: 4) {
                Text("Sonic Riders").font(.system(size: 34, weight: .heavy)).foregroundColor(.white)
                Text("Static recompilation for iPhone and iPad  ·  GXEE8P").font(.system(size: 13)).foregroundColor(kGold)
            }.padding(.leading, 30).padding(.bottom, 18)
        }
        .frame(height: 150).clipped()
        .overlay(Rectangle().fill(kGold).frame(height: 2), alignment: .bottom)
    }
}

let pages = ["Get ready", "Build", "Install", "Renew", "About"]

struct Root: View {
    @ObservedObject var m: Model
    var body: some View {
        HStack(spacing: 0) {
            VStack(alignment: .leading, spacing: 0) {
                Text("SONIC RIDERS").font(.system(size: 18, weight: .heavy)).foregroundColor(kGold).padding(.leading, 26).padding(.vertical, 22)
                ForEach(0..<pages.count, id: \.self) { i in
                    Button { m.page = i } label: {
                        HStack(spacing: 8) {
                            Text(pages[i]).font(.system(size: 15, weight: .semibold))
                                .foregroundColor(m.page == i ? kGold : kText)
                            if i == 0 && !m.ready && !m.checks.isEmpty { Circle().fill(kRed).frame(width: 7, height: 7) }
                            if i == 3, let e = m.expires, e.timeIntervalSinceNow < 2 * 86400 { Circle().fill(kYellow).frame(width: 7, height: 7) }
                        }
                        .padding(.leading, 28).frame(maxWidth: .infinity, minHeight: 40, alignment: .leading)
                        .background(m.page == i ? Color(28, 33, 50) : .clear).contentShape(Rectangle())
                    }.buttonStyle(.plain)
                }
                Spacer()
                Rectangle().fill(kLine).frame(height: 1)
                Button("Quit") { NSApp.terminate(nil) }.buttonStyle(.plain).foregroundColor(kDim).padding(.leading, 26).frame(height: 64)
            }
            .frame(width: 190).background(kSide)
            Rectangle().fill(kLine).frame(width: 1)
            VStack(spacing: 0) {
                Banner(root: m.root)
                Group {
                    switch m.page {
                    case 0: ReadyPage(m: m)
                    case 1: BuildPage(m: m)
                    case 2: InstallPage(m: m)
                    case 3: RenewPage(m: m)
                    default: AboutPage(m: m)
                    }
                }.padding(.horizontal, 30).padding(.top, 22).padding(.bottom, 12).frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
                Rectangle().fill(kLine).frame(height: 1)
                bottomBar.frame(height: 64).padding(.horizontal, 30)
            }.background(kBg)
        }
        .frame(minWidth: 960, minHeight: 680)
        .preferredColorScheme(.dark)
    }

    var bottomBar: some View {
        HStack(spacing: 10) {
            Group {
                if m.busy { HStack(spacing: 6) { ProgressView().controlSize(.small); Text(m.task + "...") }.foregroundColor(kText) }
                else if !m.status.isEmpty { Text(m.status).foregroundColor(m.failed ? kRed : kGreen) }
                else if !m.ready { Text("Get ready: \(m.checks.filter { $0.state == .missing }.count) thing(s) to set up").foregroundColor(kYellow) }
                else if m.ipaDate != nil { Text("✓ App built - install it on your device").foregroundColor(kGreen) }
                else { Text("Ready to build").foregroundColor(kGreen) }
            }.font(.system(size: 13)).lineLimit(2)
            Spacer()
            Btn(t: "Stop", enabled: m.busy) { m.stop() }
            if !m.ready && !m.checks.isEmpty {
                Btn(t: "SET UP NEXT", primary: true, enabled: !m.busy) { m.page = 0 }
            } else if m.ipaDate != nil && !m.busy && m.page != 1 {
                Btn(t: "INSTALL ON DEVICE", primary: true) { m.page = 2 }
            } else if m.iso.isEmpty || !exists(m.iso) {
                Btn(t: "SELECT DISC", primary: true, enabled: !m.busy) { m.page = 1; m.pickIso() }
            } else {
                Btn(t: m.ipaDate == nil ? "BUILD THE APP" : "BUILD AGAIN", primary: true, enabled: !m.busy && m.ready) { m.build() }
            }
        }
    }
}

// ---------------------------------------------------------------- app

final class AppDelegate: NSObject, NSApplicationDelegate {
    var window: NSWindow!
    var model: Model!

    func applicationDidFinishLaunching(_ n: Notification) {
        MainActor.assumeIsolated {
            model = Model()
            let e = ProcessInfo.processInfo.environment
            if let p = e["SR_LAUNCHER_PAGE"], let i = Int(p) { model.page = min(max(i, 0), pages.count - 1) }
            let host = NSHostingView(rootView: Root(m: model))
            window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 1060, height: 720),
                              styleMask: [.titled, .closable, .miniaturizable, .resizable], backing: .buffered, defer: false)
            window.title = "Sonic Riders Launcher"
            window.contentView = host
            window.center()
            window.isReleasedWhenClosed = false
            if let shot = e["SR_LAUNCHER_SHOT"] {
                window.setFrameOrigin(NSPoint(x: -4000, y: -4000))  // off-screen
                window.orderBack(nil)
                let take = {
                    guard let v = self.window.contentView, let rep = v.bitmapImageRepForCachingDisplay(in: v.bounds) else { exit(2) }
                    v.cacheDisplay(in: v.bounds, to: rep)
                    try? rep.representation(using: .png, properties: [:])?.write(to: URL(fileURLWithPath: shot))
                    exit(0)
                }
                if e["SR_LAUNCHER_BUILD"] != nil {
                    model.onTaskEnd = nil
                    DispatchQueue.main.asyncAfter(deadline: .now() + 4) {
                        self.model.build()
                        let end = self.model.onTaskEnd
                        self.model.onTaskEnd = { rc in end?(rc); DispatchQueue.main.asyncAfter(deadline: .now() + 2) { take() } }
                    }
                } else {
                    DispatchQueue.main.asyncAfter(deadline: .now() + 4) { take() }
                }
                return
            }
            if e["SR_LAUNCHER_BUILD"] != nil { DispatchQueue.main.asyncAfter(deadline: .now() + 4) { self.model.build() } }
            window.makeKeyAndOrderFront(nil)
            NSApp.activate(ignoringOtherApps: true)
        }
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ s: NSApplication) -> Bool { true }

    func applicationShouldTerminate(_ s: NSApplication) -> NSApplication.TerminateReply {
        MainActor.assumeIsolated { model?.stop() }
        return .terminateNow
    }
}

@main
struct Main {
    static func main() {
        let app = NSApplication.shared
        let d = AppDelegate()
        app.delegate = d
        app.setActivationPolicy(.regular)
        app.run()
    }
}
