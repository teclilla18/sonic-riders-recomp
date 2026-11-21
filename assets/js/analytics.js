(function () {
  'use strict';

  var measurementId = 'G-W75G9920MS';
  var cookiePath = '/sonic-riders-recomp';
  if (window.location.origin !== 'https://teclilla18.github.io' ||
      (window.location.pathname !== cookiePath && !window.location.pathname.startsWith(cookiePath + '/'))) return;

  var preferenceKey = 'rp:analytics-choice';
  var disableKey = 'ga-disable-' + measurementId;
  var cookiePrefix = 'ridersplus';
  var panel = document.getElementById('analytics-choice');
  var settings = document.getElementById('analytics-settings');
  var choices = panel.querySelectorAll('[data-analytics-choice]');
  var started = false;
  var preference = readPreference();

  function readPreference() {
    try { return window.localStorage.getItem(preferenceKey); }
    catch (error) { return null; }
  }

  function startAnalytics() {
    window[disableKey] = false;
    if (started) return;
    started = true;

    window.dataLayer = window.dataLayer || [];
    function gtag() { window.dataLayer.push(arguments); }
    gtag('js', new Date());
    gtag('config', measurementId, {
      allow_google_signals: false,
      allow_ad_personalization_signals: false,
      cookie_domain: window.location.hostname,
      cookie_path: cookiePath,
      cookie_prefix: cookiePrefix
    });

    var script = document.createElement('script');
    script.async = true;
    script.src = 'https://www.googletagmanager.com/gtag/js?id=' + measurementId;
    document.head.appendChild(script);
  }

  function stopAnalytics() {
    window[disableKey] = true;
    var cookieNames = document.cookie.match(/(?:^|;\s*)ridersplus_ga(?:_[A-Z0-9]+)?(?==)/gi) || [];
    cookieNames.forEach(function (cookie) {
      var name = cookie.replace(/^;\s*/, '');
      var expired = name + '=; Max-Age=0; Path=' + cookiePath + '; Secure';
      document.cookie = expired;
      document.cookie = expired + '; Domain=' + window.location.hostname;
    });
  }

  if (preference === 'allow') startAnalytics();
  else if (preference !== 'decline') panel.hidden = false;

  if (settings) {
    settings.hidden = false;
    settings.addEventListener('click', function () {
      panel.hidden = false;
      choices[0].focus();
    });
  }

  choices.forEach(function (button) {
    button.addEventListener('click', function () {
      var choice = button.getAttribute('data-analytics-choice');
      if (choice === 'allow') startAnalytics();
      else stopAnalytics();
      try { window.localStorage.setItem(preferenceKey, choice); }
      catch (error) { /* Keep the decision for this page when storage is unavailable. */ }
      panel.hidden = true;
      if (settings) settings.focus();
    });
  });

  window.addEventListener('storage', function (event) {
    if (event.key === preferenceKey && event.newValue !== 'allow') stopAnalytics();
  });
}());
