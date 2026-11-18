// Synchronous Windows Portable Devices bridge. Own source; no console/game data.
// Build with the Windows SDK: clang++ -std=c++17 -DUNICODE -D_UNICODE ...
#include <windows.h>
#include <PortableDeviceApi.h>
#include <PortableDeviceTypes.h>
#include <PortableDevice.h>
#include <propvarutil.h>
#include <wrl/client.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;

static std::string utf8(const std::wstring& s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string r(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), r.data(), n, nullptr, nullptr);
    return r;
}
static std::string json(const std::wstring& s) {
    std::string r = "\"";
    for (unsigned char c : utf8(s)) {
        if (c == '"' || c == '\\') { r += '\\'; r += c; }
        else if (c < 32) { const char* h = "0123456789abcdef"; r += "\\u00"; r += h[c >> 4]; r += h[c & 15]; }
        else r += c;
    }
    return r + '"';
}
static void check(HRESULT h, const char* what) {
    if (FAILED(h)) { char b[64]; snprintf(b, sizeof b, " (HRESULT 0x%08lX)", (unsigned long)h); throw std::runtime_error(std::string(what) + b); }
}
template<class T> static ComPtr<T> make(REFCLSID cls) {
    ComPtr<T> p; check(CoCreateInstance(cls, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&p)), "CoCreateInstance"); return p;
}
static std::wstring string_value(IPortableDeviceValues* p, REFPROPERTYKEY key) {
    PWSTR v = nullptr;
    if (FAILED(p->GetStringValue(key, &v))) return {};
    std::wstring s(v ? v : L""); CoTaskMemFree(v); return s;
}
struct Device { std::wstring id, name; };
static std::vector<Device> devices() {
    auto m = make<IPortableDeviceManager>(CLSID_PortableDeviceManager);
    DWORD n = 0; check(m->GetDevices(nullptr, &n), "Count WPD devices");
    std::vector<PWSTR> ids(n); if (n) check(m->GetDevices(ids.data(), &n), "Enumerate WPD devices");
    std::vector<Device> r;
    for (DWORD i = 0; i < n; i++) {
        DWORD z = 0; m->GetDeviceFriendlyName(ids[i], nullptr, &z);
        std::vector<WCHAR> name(z + 1);
        if (z) check(m->GetDeviceFriendlyName(ids[i], name.data(), &z), "WPD friendly name");
        r.push_back({ids[i], name.data()}); CoTaskMemFree(ids[i]);
    }
    return r;
}
struct Object { std::wstring id, name; bool folder; ULONGLONG size; };
class Wpd {
    ComPtr<IPortableDevice> device;
    ComPtr<IPortableDeviceContent> content;
    ComPtr<IPortableDeviceProperties> properties;
    std::wstring storage;
public:
    Wpd(const std::wstring& selected, bool write) {
        auto all = devices(); std::vector<Device> matches;
        for (auto& d : all) if ((!selected.empty() && d.id == selected) ||
            (selected.empty() && d.name == L"Nintendo Switch 2")) matches.push_back(d);
        if (matches.size() != 1) throw std::runtime_error("Select exactly one Nintendo Switch 2 with --device <WPD id>");
        device = make<IPortableDevice>(CLSID_PortableDeviceFTM);
        auto client = make<IPortableDeviceValues>(CLSID_PortableDeviceValues);
        check(client->SetStringValue(WPD_CLIENT_NAME, L"Sonic Riders local MTP utility"), "Client name");
        check(client->SetUnsignedIntegerValue(WPD_CLIENT_MAJOR_VERSION, 1), "Client version");
        check(client->SetUnsignedIntegerValue(WPD_CLIENT_DESIRED_ACCESS, GENERIC_READ | (write ? GENERIC_WRITE : 0)), "Client access");
        check(client->SetUnsignedIntegerValue(WPD_CLIENT_SHARE_MODE, FILE_SHARE_READ | FILE_SHARE_WRITE), "Client share");
        check(device->Open(matches[0].id.c_str(), client.Get()), "Open WPD device");
        check(device->Content(&content), "WPD content"); check(content->Properties(&properties), "WPD properties");
        std::vector<Object> cards;
        for (auto& o : children(WPD_DEVICE_OBJECT_ID)) if (o.name == L"SD Card") cards.push_back(o);
        if (cards.size() != 1) throw std::runtime_error("Expected exactly one SD Card storage");
        storage = cards[0].id;
    }
    ~Wpd() { if (device) device->Close(); }
    std::vector<Object> children(const std::wstring& parent) {
        ComPtr<IEnumPortableDeviceObjectIDs> e; check(content->EnumObjects(0, parent.c_str(), nullptr, &e), "Enumerate objects");
        std::vector<Object> r;
        for (;;) {
            PWSTR id = nullptr; DWORD got = 0; HRESULT h = e->Next(1, &id, &got); check(h, "Next object");
            if (!got) break;
            std::wstring oid(id); CoTaskMemFree(id);
            ComPtr<IPortableDeviceValues> v; check(properties->GetValues(oid.c_str(), nullptr, &v), "Read object properties");
            auto name = string_value(v.Get(), WPD_OBJECT_ORIGINAL_FILE_NAME);
            if (name.empty()) name = string_value(v.Get(), WPD_OBJECT_NAME);
            GUID type = {}; v->GetGuidValue(WPD_OBJECT_CONTENT_TYPE, &type);
            ULONGLONG size = 0; v->GetUnsignedLargeIntegerValue(WPD_OBJECT_SIZE, &size);
            r.push_back({oid, name, type == WPD_CONTENT_TYPE_FOLDER || type == WPD_CONTENT_TYPE_FUNCTIONAL_OBJECT, size});
        }
        return r;
    }
    static std::vector<std::wstring> parts(const std::wstring& path) {
        if (path.empty() || path[0] != L'/') throw std::runtime_error("Remote path must start at SD root /");
        std::vector<std::wstring> r;
        for (size_t i = 1; i < path.size();) {
            size_t end = path.find(L'/', i); if (end == std::wstring::npos) end = path.size();
            auto p = path.substr(i, end - i);
            if (p.empty() || p == L"." || p == L".." || p.find_first_of(L"\\:\0", 0, 3) != std::wstring::npos)
                throw std::runtime_error("Invalid remote path component");
            r.push_back(p); i = end + 1;
        }
        return r;
    }
    Object resolve(const std::wstring& path) {
        Object cur{storage, L"SD Card", true, 0};
        for (auto& p : parts(path)) {
            std::vector<Object> matches;
            for (auto& o : children(cur.id)) if (_wcsicmp(o.name.c_str(), p.c_str()) == 0) matches.push_back(o);
            if (matches.size() != 1) throw std::runtime_error("Remote object missing or ambiguous: " + utf8(path));
            cur = matches[0];
        }
        return cur;
    }
    std::pair<Object, std::wstring> parent(const std::wstring& path) {
        parts(path); auto at = path.find_last_of(L'/');
        if (at == std::wstring::npos || at + 1 == path.size()) throw std::runtime_error("Expected remote file path");
        auto p = resolve(at ? path.substr(0, at) : L"/");
        if (!p.folder) throw std::runtime_error("Remote parent is not a folder");
        return {p, path.substr(at + 1)};
    }
    void absent(const Object& p, const std::wstring& name) {
        for (auto& o : children(p.id)) if (_wcsicmp(o.name.c_str(), name.c_str()) == 0)
            throw std::runtime_error("Refusing to overwrite existing remote object: " + utf8(name));
    }
    void list(const std::wstring& path) {
        auto p = resolve(path); if (!p.folder) throw std::runtime_error("List expects a folder");
        std::cout << '['; bool first = true;
        for (auto& o : children(p.id)) {
            if (!first) std::cout << ','; first = false;
            std::cout << "{\"name\":" << json(o.name) << ",\"folder\":" << (o.folder ? "true" : "false") << ",\"size\":" << o.size << '}';
        }
        std::cout << "]\n";
    }
    void get(const std::wstring& path, const fs::path& local) {
        auto o = resolve(path); if (o.folder) throw std::runtime_error("Get expects a file");
        if (fs::exists(local)) throw std::runtime_error("Refusing to overwrite local file");
        ComPtr<IPortableDeviceResources> resources; check(content->Transfer(&resources), "WPD resources");
        ComPtr<IStream> stream; DWORD optimal = 0;
        check(resources->GetStream(o.id.c_str(), WPD_RESOURCE_DEFAULT, STGM_READ, &optimal, &stream), "Open read stream");
        std::ofstream f(local, std::ios::binary); if (!f) throw std::runtime_error("Open local output failed");
        std::vector<char> buf(1024 * 1024); ULONGLONG total = 0;
        for (;;) {
            ULONG got = 0; HRESULT h = stream->Read(buf.data(), (ULONG)buf.size(), &got); check(h, "Read MTP stream");
            if (got) { f.write(buf.data(), got); if (!f) throw std::runtime_error("Local output write failed"); total += got; }
            if (h == S_FALSE || !got) break;
        }
        f.close();
        if (!f || total != o.size) throw std::runtime_error("Readback size or local flush mismatch");
        std::cout << "{\"bytes\":" << total << "}\n";
    }
    void put(const fs::path& local, const std::wstring& path) {
        auto p = parent(path); absent(p.first, p.second);
        std::ifstream f(local, std::ios::binary); if (!f) throw std::runtime_error("Open local input failed");
        auto size = fs::file_size(local);
        auto v = make<IPortableDeviceValues>(CLSID_PortableDeviceValues);
        check(v->SetStringValue(WPD_OBJECT_PARENT_ID, p.first.id.c_str()), "Set parent");
        check(v->SetStringValue(WPD_OBJECT_NAME, p.second.c_str()), "Set name");
        check(v->SetStringValue(WPD_OBJECT_ORIGINAL_FILE_NAME, p.second.c_str()), "Set filename");
        check(v->SetUnsignedLargeIntegerValue(WPD_OBJECT_SIZE, size), "Set size");
        check(v->SetGuidValue(WPD_OBJECT_CONTENT_TYPE, WPD_CONTENT_TYPE_GENERIC_FILE), "Set content type");
        check(v->SetGuidValue(WPD_OBJECT_FORMAT, WPD_OBJECT_FORMAT_UNSPECIFIED), "Set format");
        ComPtr<IStream> stream; DWORD optimal = 0; PWSTR cookie = nullptr;
        check(content->CreateObjectWithPropertiesAndData(v.Get(), &stream, &optimal, &cookie), "Create MTP object");
        CoTaskMemFree(cookie);
        try {
            std::vector<char> buf(1024 * 1024); ULONGLONG total = 0;
            while (f) {
                f.read(buf.data(), buf.size()); auto got = f.gcount();
                for (ULONG off = 0; off < (ULONG)got;) {
                    ULONG wrote = 0; check(stream->Write(buf.data() + off, (ULONG)got - off, &wrote), "Write MTP stream");
                    if (!wrote) throw std::runtime_error("MTP write made no progress"); off += wrote; total += wrote;
                }
            }
            if (total != size || f.bad()) throw std::runtime_error("Local input changed or read failed");
            check(stream->Commit(STGC_DEFAULT), "Commit MTP upload");
            std::cout << "{\"bytes\":" << total << "}\n";
        } catch (...) { stream->Revert(); throw; }
    }
    static void mutation_allowed(const std::wstring& path, bool allow) {
        auto p = parts(path);
        if (p.empty() || p[0] != L"switch") throw std::runtime_error("Mutations are restricted to /switch");
        if (!allow && p.back().rfind(L".codex-mtp-", 0) != 0)
            throw std::runtime_error("Non-probe mutation requires explicit --allow-installed");
    }
    void rename(const std::wstring& path, const std::wstring& name, bool allow) {
        mutation_allowed(path, allow); auto p = parent(path); parts(L"/" + name);
        if (name.find_first_of(L"/\\") != std::wstring::npos) throw std::runtime_error("Rename takes a basename");
        mutation_allowed(path.substr(0, path.find_last_of(L'/') + 1) + name, allow);
        absent(p.first, name); auto o = resolve(path); if (o.folder) throw std::runtime_error("Refusing folder rename");
        auto v = make<IPortableDeviceValues>(CLSID_PortableDeviceValues);
        check(v->SetStringValue(WPD_OBJECT_ORIGINAL_FILE_NAME, name.c_str()), "Set rename filename");
        ComPtr<IPortableDeviceValues> results;
        check(properties->SetValues(o.id.c_str(), v.Get(), &results), "Rename MTP object");
        HRESULT result = S_OK;
        if (results && SUCCEEDED(results->GetErrorValue(WPD_OBJECT_ORIGINAL_FILE_NAME, &result))) check(result, "Rename filename result");
        std::cout << "{\"renamed\":true}\n";
    }
    void remove(const std::wstring& path, bool allow) {
        mutation_allowed(path, allow); auto o = resolve(path); if (o.folder) throw std::runtime_error("Refusing folder deletion");
        auto ids = make<IPortableDevicePropVariantCollection>(CLSID_PortableDevicePropVariantCollection);
        PROPVARIANT id; PropVariantInit(&id); check(InitPropVariantFromString(o.id.c_str(), &id), "Delete object ID");
        HRESULT h = ids->Add(&id); PropVariantClear(&id); check(h, "Delete collection");
        ComPtr<IPortableDevicePropVariantCollection> results;
        check(content->Delete(0, ids.Get(), &results), "Delete MTP file");
        if (results) {
            PROPVARIANT value; PropVariantInit(&value);
            h = results->GetAt(0, &value);
            if (SUCCEEDED(h) && value.vt == VT_ERROR) h = value.scode;
            PropVariantClear(&value); check(h, "Delete file result");
        }
        std::cout << "{\"deleted\":true}\n";
    }
};
int wmain(int argc, wchar_t** argv) {
    HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    try {
        check(init, "Initialize COM"); int i = 1; std::wstring selected; bool allow = false;
        while (i < argc && std::wstring(argv[i]).rfind(L"--", 0) == 0) {
            if (std::wstring(argv[i]) == L"--device" && i + 1 < argc) { selected = argv[i + 1]; i += 2; }
            else if (std::wstring(argv[i]) == L"--allow-installed") { allow = true; i++; }
            else throw std::runtime_error("Unknown option");
        }
        if (i >= argc) throw std::runtime_error("Usage: switch2_wpd [--device id] [--allow-installed] devices|list|get|put-new|rename|delete ...");
        std::wstring cmd = argv[i++];
        if (cmd == L"devices") {
            auto all = devices(); std::cout << '[';
            for (size_t j = 0; j < all.size(); j++) { if (j) std::cout << ','; std::cout << "{\"id\":" << json(all[j].id) << ",\"name\":" << json(all[j].name) << '}'; }
            std::cout << "]\n";
        } else {
            Wpd wpd(selected, cmd != L"get" && cmd != L"list");
            int n = argc - i;
            if (cmd == L"list" && n <= 1) wpd.list(n ? argv[i] : L"/");
            else if (cmd == L"get" && n == 2) wpd.get(argv[i], argv[i + 1]);
            else if (cmd == L"put-new" && n == 2) { Wpd::mutation_allowed(argv[i + 1], allow); wpd.put(argv[i], argv[i + 1]); }
            else if (cmd == L"rename" && n == 2) wpd.rename(argv[i], argv[i + 1], allow);
            else if (cmd == L"delete" && n == 1) wpd.remove(argv[i], allow);
            else throw std::runtime_error("Invalid command or argument count");
        }
        CoUninitialize(); return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; if (SUCCEEDED(init)) CoUninitialize(); return 1; }
}
