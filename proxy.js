const http = require("http");
const https = require("https");
const { URL } = require("url");

const PORT = 3000;

function fetchWithCookies(url, opts = {}, jar = {}) {
  return new Promise((resolve, reject) => {
    const u = new URL(url);
    const lib = u.protocol === "https:" ? https : http;
    const headers = Object.assign({}, opts.headers || {});
    const cookieStr = Object.entries(jar)
      .map(([k, v]) => `${k}=${v}`)
      .join("; ");
    if (cookieStr) headers["Cookie"] = cookieStr;
    headers["User-Agent"] =
      "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0 Safari/537.36";
    const req = lib.request(
      {
        method: opts.method || "GET",
        hostname: u.hostname,
        port: u.port || (u.protocol === "https:" ? 443 : 80),
        path: u.pathname + u.search,
        headers: headers,
      },
      (res) => {
        let data = "";
        res.on("data", (chunk) => (data += chunk));
        res.on("end", () => {
          const setCookies = res.headers["set-cookie"] || [];
          setCookies.forEach((sc) => {
            const m = sc.match(/^([^=]+)=([^;]+)/);
            if (m) jar[m[1]] = m[2];
          });
          resolve({
            status: res.statusCode,
            headers: res.headers,
            body: data,
            jar,
          });
        });
      },
    );
    req.on("error", reject);
    if (opts.body) req.write(opts.body);
    req.end();
  });
}

async function logSteptember(email, password, steps, dateStr) {
  const jar = {};
  let r = await fetchWithCookies(
    "https://www.steptember.org.au/login",
    { headers: { "X-Requested-With": "XMLHttpRequest" } },
    jar,
  );
  const m = r.body.match(/name="CSRFToken"\s+value="([^"]+)"/);
  if (!m) throw new Error("no CSRF");
  const token = m[1];
  const body = `CSRFToken=${encodeURIComponent(token)}&login_email=${encodeURIComponent(email)}&login_password=${encodeURIComponent(password)}`;
  r = await fetchWithCookies(
    "https://www.steptember.org.au/login",
    {
      method: "POST",
      headers: {
        "X-Requested-With": "XMLHttpRequest",
        Referer: "https://www.steptember.org.au/login",
        "Content-Type": "application/x-www-form-urlencoded",
      },
      body: body,
    },
    jar,
  );
  if (r.body.includes('id="form-login"')) throw new Error("login fail");
  // follow JS redirect if present
  const redir = r.body.match(/window\.location\.href="([^"]+)"/);
  if (redir) {
    await fetchWithCookies(redir[1], {}, jar);
  }
  r = await fetchWithCookies(
    "https://www.steptember.org.au/login/activity",
    {},
    jar,
  );
  if (r.body.includes('id="form-login"')) throw new Error("session fail");
  const csrf2 = (r.body.match(/name="CSRFToken"\s+value="([^"]+)"/) || [])[1];
  const hid = (r.body.match(/history_id\s*=\s*(\d+)/) || [])[1];
  if (!csrf2 || !hid) throw new Error("parse activity fail");
  const payload = JSON.stringify({
    steps: parseInt(steps, 10),
    date_from: dateStr,
    history_id: parseInt(hid, 10),
    source: "manual",
  });
  r = await fetchWithCookies(
    "https://www.steptember.org.au/customcode/web_validatesteps",
    {
      method: "POST",
      headers: {
        "X-Requested-With": "XMLHttpRequest",
        Referer: "https://www.steptember.org.au/login/activity",
        "Content-Type": "application/json",
      },
      body: payload,
    },
    jar,
  );
  let vok = false;
  try {
    vok = JSON.parse(r.body).success === true;
  } catch (e) {}
  console.log("validate", vok, r.body.slice(0, 200));
  const formFields = {};
  const re = /<input[^>]*name="([^"]+)"[^>]*value="([^"]*)"[^>]*>/gi;
  let mm;
  while ((mm = re.exec(r.body)) !== null)
    if (mm[1]) formFields[mm[1]] = mm[2] || "";
  // Actually need to parse from activity page html, not validate response. Use previous r.body from activity page
  // For simplicity, reconstruct minimal form
  const formBody = `CSRFToken=${encodeURIComponent(csrf2)}&date_from=${encodeURIComponent(dateStr)}&steps=${encodeURIComponent(steps)}&activity_type=&duration=`;
  // Need to also include other hidden fields from activity page; parse again
  const activityHtml = (
    await fetchWithCookies(
      "https://www.steptember.org.au/login/activity",
      {},
      jar,
    )
  ).body;
  const hiddenRe = /<input[^>]*name="([^"]+)"[^>]*value="([^"]*)"[^>]*>/gi;
  const allFields = {};
  let hm;
  while ((hm = hiddenRe.exec(activityHtml)) !== null)
    if (hm[1]) allFields[hm[1]] = hm[2] || "";
  allFields["CSRFToken"] = csrf2;
  allFields["date_from"] = dateStr;
  allFields["steps"] = String(steps);
  allFields["activity_type"] = "";
  allFields["duration"] = "";
  const addBody = Object.entries(allFields)
    .map(([k, v]) => `${encodeURIComponent(k)}=${encodeURIComponent(v)}`)
    .join("&");
  r = await fetchWithCookies(
    "https://www.steptember.org.au/customcode/web_addactivity",
    {
      method: "POST",
      headers: {
        Referer: "https://www.steptember.org.au/login/activity",
        "Content-Type": "application/x-www-form-urlencoded",
      },
      body: addBody,
    },
    jar,
  );
  console.log("add", r.status, r.body.slice(0, 300));
  r = await fetchWithCookies(
    "https://www.steptember.org.au/login/activity",
    {},
    jar,
  );
  const ok = r.body.includes(dateStr) && r.body.includes(String(steps));
  return { ok, validate: vok, activityHtml: r.body.slice(0, 500) };
}

const server = http.createServer(async (req, res) => {
  res.setHeader("Access-Control-Allow-Origin", "*");
  res.setHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  res.setHeader("Access-Control-Allow-Headers", "Content-Type");
  if (req.method === "OPTIONS") {
    res.writeHead(200);
    res.end();
    return;
  }
  if (req.url === "/log" && req.method === "POST") {
    let body = "";
    req.on("data", (chunk) => (body += chunk));
    req.on("end", async () => {
      try {
        const j = JSON.parse(body);
        console.log("proxy request", j.email, j.steps, j.date);
        const result = await logSteptember(
          j.email,
          j.password,
          j.steps,
          j.date,
        );
        res.writeHead(200, { "Content-Type": "application/json" });
        res.end(JSON.stringify({ success: true, result }));
      } catch (e) {
        console.error(e);
        res.writeHead(500, { "Content-Type": "application/json" });
        res.end(JSON.stringify({ success: false, error: e.message }));
      }
    });
    return;
  }
  if (req.url === "/health") {
    res.writeHead(200);
    res.end("ok");
    return;
  }
  res.writeHead(404);
  res.end("not found");
});

server.listen(PORT, () => console.log("proxy listening on " + PORT));
