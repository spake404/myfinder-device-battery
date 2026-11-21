const DEFAULT_REPO = "spake404/myfinder-device-battery";
const DEFAULT_MAINTAINER = "spake404";

const CATEGORY_LABELS = {
  device: "设备不识别",
  battery: "电量或 DPI 不对",
  ui: "界面或操作",
  other: "其他",
};

const REPLY_KEY_PATTERN = /^[A-Za-z0-9_-]{42}[AEIMQUYcgkosw048]$/;
const REQUEST_ID_PATTERN = /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/;

function jsonResponse(body, status = 200, extraHeaders = {}) {
  return new Response(JSON.stringify(body), {
    status,
    headers: {
      "content-type": "application/json; charset=utf-8",
      "cache-control": "no-store",
      ...extraHeaders,
    },
  });
}

async function sha256Hex(text) {
  const data = new TextEncoder().encode(text);
  const digest = await crypto.subtle.digest("SHA-256", data);
  return Array.from(new Uint8Array(digest))
    .map((b) => b.toString(16).padStart(2, "0"))
    .join("");
}

function validInput(category, description) {
  if (!Object.prototype.hasOwnProperty.call(CATEGORY_LABELS, category)) return false;
  if (typeof description !== "string") return false;
  const trimmed = description.trim();
  return trimmed.length >= 5 && trimmed.length <= 2000;
}

function extractReplyKeyHash(issueBody) {
  if (typeof issueBody !== "string") return null;
  const match = issueBody.match(/<!--\s*myfinder-reply-key-sha256:\s*([0-9a-f]{64})\s*-->/);
  return match ? match[1] : null;
}

function extractRequestId(commentBody) {
  if (typeof commentBody !== "string") return null;
  const match = commentBody.match(/<!--\s*myfinder-request-id:\s*([0-9a-f-]{36})\s*-->/);
  return match ? match[1] : null;
}

function cleanCommentText(text) {
  if (typeof text !== "string") return "";
  const cleaned = text.replace(/<!--[\s\S]*?-->/g, "").trim();
  return cleaned.length > 8000 ? cleaned.slice(0, 8000) : cleaned;
}

async function githubRequest(env, path, method = "GET", body = null) {
  const repo = env.GITHUB_REPOSITORY || DEFAULT_REPO;
  const url = `https://api.github.com/repos/${repo}${path}`;
  const headers = {
    "accept": "application/vnd.github+json",
    "authorization": `Bearer ${env.GITHUB_TOKEN}`,
    "user-agent": "MyFinder-Feedback-Relay/1.0",
    "x-github-api-version": "2022-11-28",
  };
  if (body !== null) {
    headers["content-type"] = "application/json; charset=utf-8";
  }
  return fetch(url, {
    method,
    headers,
    body: body !== null ? JSON.stringify(body) : undefined,
  });
}

async function handleCreateFeedback(request, env) {
  let payload;
  try {
    payload = await request.json();
  } catch {
    return jsonResponse({ error: "invalid" }, 400);
  }

  const category = payload?.category;
  const description = typeof payload?.description === "string" ? payload.description.trim() : "";
  const diagnostics = typeof payload?.diagnostics === "string" ? payload.diagnostics.trim() : "";
  const appVersion = typeof payload?.appVersion === "string" ? payload.appVersion.trim() : "";
  const replyKey = payload?.replyKey ?? null;

  if (!validInput(category, description)) {
    return jsonResponse({ error: "invalid" }, 400);
  }
  if (diagnostics.length > 20000) {
    return jsonResponse({ error: "too_large" }, 413);
  }
  if (replyKey !== null && (typeof replyKey !== "string" || !REPLY_KEY_PATTERN.test(replyKey))) {
    return jsonResponse({ error: "invalid" }, 400);
  }

  const label = CATEGORY_LABELS[category];
  const firstLine = description.split(/\r?\n/)[0].slice(0, 60);
  const title = `[${label}] ${firstLine}`;

  const sections = [
    `### 反馈类型\n${label} (\`${category}\`)`,
    `### 问题描述\n${description}`,
  ];
  if (appVersion) {
    sections.push(`### 程序版本\n\`${appVersion}\``);
  }
  if (diagnostics) {
    sections.push(`### 诊断摘要\n\`\`\`text\n${diagnostics}\n\`\`\``);
  }
  if (replyKey) {
    const keyHash = await sha256Hex(replyKey);
    sections.push(`<!-- myfinder-reply-key-sha256: ${keyHash} -->`);
  }

  const ghResponse = await githubRequest(env, "/issues", "POST", {
    title,
    body: sections.join("\n\n"),
  });

  if (ghResponse.status === 429) {
    return jsonResponse({ error: "rate_limited" }, 429);
  }
  if (!ghResponse.ok) {
    return jsonResponse({ error: "unavailable" }, 503);
  }

  const created = await ghResponse.json();
  const repo = env.GITHUB_REPOSITORY || DEFAULT_REPO;
  const number = Number(created.number);
  return jsonResponse(
    {
      number,
      url: `https://github.com/${repo}/issues/${number}`,
      replyAccess: Boolean(replyKey),
    },
    201
  );
}

async function handlePollReplies(request, env) {
  let payload;
  try {
    payload = await request.json();
  } catch {
    return jsonResponse({ error: "invalid" }, 400);
  }

  if (payload?.schema !== 1 || !Array.isArray(payload?.numbers) || payload.numbers.length > 10) {
    return jsonResponse({ error: "invalid" }, 400);
  }

  const maintainer = (env.MAINTAINER_LOGIN || DEFAULT_MAINTAINER).toLowerCase();
  const items = [];

  for (const rawNumber of payload.numbers) {
    const number = Number(rawNumber);
    if (!Number.isInteger(number) || number <= 0) {
      items.push({ number: rawNumber, status: "error" });
      continue;
    }

    const [issueRes, commentsRes] = await Promise.all([
      githubRequest(env, `/issues/${number}`),
      githubRequest(env, `/issues/${number}/comments?per_page=100`),
    ]);

    if (!issueRes.ok || !commentsRes.ok) {
      items.push({ number, status: "unavailable" });
      continue;
    }

    const issue = await issueRes.json();
    const rawComments = await commentsRes.json();
    const state = issue.state === "closed" ? "closed" : "open";
    const locked = Boolean(issue.locked);
    const hasKey = extractReplyKeyHash(issue.body) !== null;
    const canSupplement = state === "open" && !locked && hasKey;

    const comments = [];
    if (Array.isArray(rawComments)) {
      for (const c of rawComments) {
        if (!c || typeof c.body !== "string") continue;
        // Skip user supplements posted through this relay
        if (extractRequestId(c.body) !== null) continue;

        const login = (c.user?.login || "").toLowerCase();
        const assoc = c.author_association || "";
        const isMaintainer =
          login === maintainer ||
          assoc === "OWNER" ||
          assoc === "MEMBER" ||
          assoc === "COLLABORATOR";
        if (!isMaintainer) continue;

        const text = cleanCommentText(c.body);
        if (!text) continue;

        comments.push({
          id: String(c.id),
          author: "maintainer",
          text,
          createdAt: c.created_at,
          updatedAt: c.updated_at || c.created_at,
        });
        if (comments.length >= 50) break;
      }
    }

    items.push({
      number,
      status: "ok",
      state,
      locked,
      canSupplement,
      comments,
    });
  }

  return jsonResponse({ schema: 1, items }, 200);
}

async function handleSupplement(request, env) {
  let payload;
  try {
    payload = await request.json();
  } catch {
    return jsonResponse({ error: "invalid" }, 400);
  }

  const number = Number(payload?.number);
  const replyKey = payload?.replyKey;
  const requestId = payload?.requestId;
  const description = typeof payload?.description === "string" ? payload.description.trim() : "";
  const diagnostics = typeof payload?.diagnostics === "string" ? payload.diagnostics.trim() : "";
  const appVersion = typeof payload?.appVersion === "string" ? payload.appVersion.trim() : "";

  if (
    payload?.schema !== 1 ||
    !Number.isInteger(number) ||
    number <= 0 ||
    typeof replyKey !== "string" ||
    !REPLY_KEY_PATTERN.test(replyKey) ||
    typeof requestId !== "string" ||
    !REQUEST_ID_PATTERN.test(requestId) ||
    description.length < 5 ||
    description.length > 2000
  ) {
    return jsonResponse({ error: "invalid" }, 400);
  }

  if (diagnostics.length > 20000) {
    return jsonResponse({ error: "too_large" }, 413);
  }

  const issueRes = await githubRequest(env, `/issues/${number}`);
  if (issueRes.status === 404) {
    return jsonResponse({ error: "denied" }, 403);
  }
  if (!issueRes.ok) {
    return jsonResponse({ error: "unavailable" }, 503);
  }

  const issue = await issueRes.json();
  const expectedHash = extractReplyKeyHash(issue.body);
  const actualHash = await sha256Hex(replyKey);
  if (!expectedHash || expectedHash !== actualHash) {
    return jsonResponse({ error: "denied" }, 403);
  }

  if (issue.state === "closed" || issue.locked) {
    return jsonResponse({ error: "closed" }, 409);
  }

  const commentsRes = await githubRequest(env, `/issues/${number}/comments?per_page=100`);
  if (commentsRes.ok) {
    const existingComments = await commentsRes.json();
    if (Array.isArray(existingComments)) {
      for (const c of existingComments) {
        if (extractRequestId(c?.body) === requestId) {
          return jsonResponse(
            {
              number,
              commentId: String(c.id),
              duplicate: true,
            },
            200
          );
        }
      }
    }
  }

  const parts = [`### 用户补充资料\n${description}`];
  if (appVersion) {
    parts.push(`**程序版本**：\`${appVersion}\``);
  }
  if (diagnostics) {
    parts.push(`### 诊断摘要\n\`\`\`text\n${diagnostics}\n\`\`\``);
  }
  parts.push(`<!-- myfinder-request-id: ${requestId} -->`);

  const createRes = await githubRequest(env, `/issues/${number}/comments`, "POST", {
    body: parts.join("\n\n"),
  });

  if (createRes.status === 429) {
    return jsonResponse({ error: "rate_limited" }, 429);
  }
  if (!createRes.ok) {
    return jsonResponse({ error: "unavailable" }, 503);
  }

  const created = await createRes.json();
  return jsonResponse(
    {
      number,
      commentId: String(created.id),
      duplicate: false,
    },
    201
  );
}

export default {
  async fetch(request, env) {
    if (!env.GITHUB_TOKEN) {
      return jsonResponse({ error: "unavailable" }, 503);
    }
    const url = new URL(request.url);
    if (request.method !== "POST") {
      return jsonResponse({ error: "not_found" }, 404);
    }
    if (url.pathname === "/feedback") {
      return handleCreateFeedback(request, env);
    }
    if (url.pathname === "/feedback/replies") {
      return handlePollReplies(request, env);
    }
    if (url.pathname === "/feedback/supplement") {
      return handleSupplement(request, env);
    }
    return jsonResponse({ error: "not_found" }, 404);
  },
};
