var CONFIG_PAGE = [
  "<!DOCTYPE html><html><head>",
  '<meta name="viewport" content="width=device-width,initial-scale=1">',
  "<style>",
  "body{background:#1a1a1a;color:#fff;font-family:sans-serif;padding:20px;margin:0}",
  "h2{margin-top:0}",
  "button{display:block;width:100%;padding:14px;margin:8px 0;border:none;",
  "border-radius:8px;font-size:16px;cursor:pointer;font-weight:bold}",
  "</style></head><body>",
  "<h2>Time Colour</h2>",
  '<button style="background:#fff;color:#000" onclick="send(0)">White</button>',
  '<button style="background:#0f0;color:#000" onclick="send(1)">Green</button>',
  '<button style="background:#ff0;color:#000" onclick="send(2)">Yellow</button>',
  '<button style="background:#0ff;color:#000" onclick="send(3)">Cyan</button>',
  '<button style="background:#f90;color:#000" onclick="send(4)">Orange</button>',
  "<script>",
  "function send(v){",
  '  location.href="pebblejs://close#"+encodeURIComponent(JSON.stringify({colorIndex:v}));',
  "}",
  "</script></body></html>"
].join("");

Pebble.addEventListener("ready", function() {
  console.log("JS ready");
});

Pebble.addEventListener("showConfiguration", function() {
  Pebble.openURL("data:text/html," + encodeURIComponent(CONFIG_PAGE));
});

Pebble.addEventListener("webviewclosed", function(e) {
  if (!e.response || e.response === "CANCELLED") {
    return;
  }
  try {
    var config = JSON.parse(decodeURIComponent(e.response));
    Pebble.sendAppMessage(
      { COLOR_INDEX: config.colorIndex },
      function() {
        console.log("Settings sent");
      },
      function(err) {
        console.log("Settings error: " + err);
      }
    );
  } catch (ex) {
    console.log("Config parse error: " + ex);
  }
});
