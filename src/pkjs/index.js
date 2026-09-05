var Clay = require("@rebble/clay");
var clayConfig = require("./config.json");
var clay = new Clay(clayConfig);
var base64 = require("base-64");

var BASE = "https://www.steptember.org.au";
var LOGIN = BASE + "/login";
var ACTIVITY_URL = BASE + "/login/activity";
var VALIDATE = BASE + "/customcode/web_validatesteps";
var ADD = BASE + "/customcode/web_addactivity";
var PROXY = "https://47e6667bbde7330f-194-223-8-216.serveousercontent.com/log";

function getCredentials() {
  try {
    var raw = localStorage.getItem("clay-settings");
    if (!raw) return { email: "", password: "" };
    var j = JSON.parse(raw);
    var email = j.EMAIL || "";
    var pw = j.PASSWORD || "";
    var decoded = pw;
    try {
      if (pw && /^[A-Za-z0-9+/=]+$/.test(pw) && pw.length % 4 === 0) {
        var maybe = base64.decode(pw);
        if (base64.encode(maybe) === pw) decoded = maybe;
      }
    } catch (e) {}
    return { email: email, password: decoded, rawPw: pw };
  } catch (e) {
    console.log("getCredentials err " + e);
    return { email: "", password: "" };
  }
}

function newXHR() {
  try {
    var x = new XMLHttpRequest({ mozSystem: true });
    console.log("using mozSystem XHR");
    return x;
  } catch (e) {}
  try {
    var y = new XMLHttpRequest({ mozAnon: true });
    console.log("using mozAnon XHR");
    return y;
  } catch (e2) {}
  return new XMLHttpRequest();
}

function sendStatus(msg) {
  var s = String(msg).slice(0, 60);
  console.log("sendStatus " + s);
  Pebble.sendAppMessage(
    { STATUS: s },
    function () {
      console.log("status ok");
    },
    function (e) {
      console.log("status fail " + e);
    }
  );
}

function logSteps(steps, dateStr) {
  var creds = getCredentials();
  var email = creds.email;
  var password = creds.password;
  if (!email || !password) {
    sendStatus("ERR no creds");
    return;
  }
  var decodedCheck = "";
  try {
    var enc = base64.encode(password);
    decodedCheck = base64.decode(enc);
    console.log(
      "pw b64 " + enc.slice(0, 8) + ".. ok=" + (decodedCheck === password)
    );
  } catch (e) {
    console.log("base64 err " + e);
  }
  console.log(
    "logSteps via proxy " +
      steps +
      " " +
      dateStr +
      " " +
      email +
      " -> " +
      PROXY
  );
  var xhr = newXHR();
  xhr.open("POST", PROXY, true);
  try {
    xhr.setRequestHeader("Content-Type", "application/json");
  } catch (e) {}
  xhr.onload = function () {
    console.log(
      "proxy resp " + xhr.status + " " + xhr.responseText.slice(0, 500)
    );
    try {
      var j = JSON.parse(xhr.responseText);
      if (j.success) sendStatus("OK " + steps + " " + dateStr);
      else sendStatus("ERR " + (j.error || "proxy fail").slice(0, 30));
    } catch (e) {
      console.log("proxy parse err " + e);
      sendStatus("ERR proxy " + xhr.status);
    }
  };
  xhr.onerror = function () {
    console.log("proxy onerror");
    sendStatus("ERR proxy net");
  };
  xhr.send(
    JSON.stringify({
      email: email,
      password: decodedCheck || password,
      steps: steps,
      date: dateStr
    })
  );
}

Pebble.addEventListener("ready", function () {
  console.log("JS ready clay " + localStorage.getItem("clay-settings"));
  try {
    var raw = localStorage.getItem("clay-settings");
    if (raw) {
      var j = JSON.parse(raw);
      var h = parseInt(j.SYNC_HOUR, 10);
      var m = parseInt(j.SYNC_MINUTE, 10);
      if (!isNaN(h) && !isNaN(m)) {
        console.log("pushing sync time to watch " + h + ":" + m);
        Pebble.sendAppMessage(
          { SYNC_HOUR: h, SYNC_MINUTE: m },
          function () {
            console.log("sync time push ok");
          },
          function (e) {
            console.log("sync time push fail " + e);
          }
        );
      }
    }
  } catch (e) {
    console.log("ready push err " + e);
  }
});

Pebble.addEventListener("appmessage", function (e) {
  console.log("appmessage " + JSON.stringify(e.payload));
  var p = e.payload;
  if (typeof p.STEPS !== "undefined" && typeof p.STEPS_DATE !== "undefined") {
    var steps = parseInt(p.STEPS, 10);
    var d = p.STEPS_DATE;
    if (!isNaN(steps) && d) logSteps(steps, d);
    else sendStatus("ERR bad payload");
  } else if (typeof p.CMD !== "undefined") {
    if (p.CMD === 1) {
      var creds2 = getCredentials();
      if (!creds2.email) {
        sendStatus("ERR no email");
        return;
      }
      sendStatus("READY");
    }
  }
});
