/**
 * Login page with GitHub and QQ OAuth buttons.
 *
 * GitHub uses PKCE (RFC 7636): the browser generates a code_verifier and
 * code_challenge, then redirects directly to GitHub.  The server cannot
 * reach GitHub (GFW), so all GitHub API calls happen in the browser.
 * Identity is verified client-side and POSTed to the backend /exchange.
 */

import { useSearchParams } from 'react-router-dom';
import { useAuth } from '../lib/auth';
import { Navigate } from 'react-router-dom';
import { Github } from 'lucide-react';

// ── PKCE helpers ────────────────────────────────────────────────────────────

/**
 * Pure-JS SHA-256 — works on ANY origin (HTTP or HTTPS).
 *
 * We cannot use crypto.subtle.digest() because the production server serves
 * on HTTP (not a secure context), and SubtleCrypto is undefined there.
 *
 * This is a self-contained implementation of FIPS 180-4; ~1 KiB gzipped.
 * It only needs to hash one short string per login, so performance is
 * irrelevant — correctness and portability are what matter.
 */

// SHA-256 constants (first 32 bits of the fractional parts of the cube roots
// of the first 64 primes).
const K: number[] = [
  0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
  0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
  0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
  0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
  0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
  0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
  0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
  0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
  0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
  0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
  0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
];

function rotr(x: number, n: number): number {
  return (x >>> n) | (x << (32 - n));
}

function sha256(message: Uint8Array): Uint8Array {
  // Initial hash values (first 32 bits of the fractional parts of the square
  // roots of the first 8 primes).
  const H: number[] = [
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
  ];

  // Pre-processing — pad the message
  const msgBitLen = message.length * 8;
  // Number of zero bytes needed after the 0x80 sentinel so that
  // (msgLen + 1 + zeroBytes) % 64 ≡ 56  (because 56 × 8 = 448).
  let zeroBytes = 56 - ((message.length + 1) % 64);
  if (zeroBytes < 0) zeroBytes += 64;
  const totalLen = message.length + 1 + zeroBytes + 8;
  const padded = new Uint8Array(totalLen);
  padded.set(message);
  padded[message.length] = 0x80; // append '1' bit + 7 zero bits
  // Append 64-bit big-endian length.  Our messages are short (< 512 MiB),
  // so the upper 32 bits are always 0.
  const view = new DataView(padded.buffer);
  view.setUint32(totalLen - 4, msgBitLen, false);       // lower 32 bits
  view.setUint32(totalLen - 8, 0, false);               // upper 32 bits = 0

  // Process each 512-bit (64-byte) chunk
  for (let off = 0; off < padded.length; off += 64) {
    const W = new Array<number>(64);

    // Prepare message schedule
    for (let t = 0; t < 16; t++) {
      W[t] =
        (padded[off + t * 4]! << 24) |
        (padded[off + t * 4 + 1]! << 16) |
        (padded[off + t * 4 + 2]! << 8) |
        padded[off + t * 4 + 3]!;
    }
    for (let t = 16; t < 64; t++) {
      const s0 = rotr(W[t - 15]!, 7) ^ rotr(W[t - 15]!, 18) ^ (W[t - 15]! >>> 3);
      const s1 = rotr(W[t - 2]!, 17) ^ rotr(W[t - 2]!, 19) ^ (W[t - 2]! >>> 10);
      W[t] = (W[t - 16]! + s0 + W[t - 7]! + s1) >>> 0;
    }

    // Initialise working variables
    let [a, b, c, d, e, f, g, h] = H;

    // Compression function
    for (let t = 0; t < 64; t++) {
      const S1 = rotr(e!, 6) ^ rotr(e!, 11) ^ rotr(e!, 25);
      const ch = (e! & f!) ^ (~e! & g!);
      const temp1 = (h! + S1 + ch + K[t]! + W[t]!) >>> 0;
      const S0 = rotr(a!, 2) ^ rotr(a!, 13) ^ rotr(a!, 22);
      const maj = (a! & b!) ^ (a! & c!) ^ (b! & c!);
      const temp2 = (S0 + maj) >>> 0;

      h = g; g = f; f = e; e = (d! + temp1) >>> 0;
      d = c; c = b; b = a; a = (temp1 + temp2) >>> 0;
    }

    // Update hash values
    H[0] = (H[0]! + a!) >>> 0; H[1] = (H[1]! + b!) >>> 0;
    H[2] = (H[2]! + c!) >>> 0; H[3] = (H[3]! + d!) >>> 0;
    H[4] = (H[4]! + e!) >>> 0; H[5] = (H[5]! + f!) >>> 0;
    H[6] = (H[6]! + g!) >>> 0; H[7] = (H[7]! + h!) >>> 0;
  }

  // Produce final hash
  const out = new Uint8Array(32);
  for (let i = 0; i < 8; i++) {
    out[i * 4]     = (H[i]! >>> 24) & 0xff;
    out[i * 4 + 1] = (H[i]! >>> 16) & 0xff;
    out[i * 4 + 2] = (H[i]! >>> 8)  & 0xff;
    out[i * 4 + 3] =  H[i]!         & 0xff;
  }
  return out;
}

/** Generate a cryptographically random string for PKCE. */
function generateCodeVerifier(): string {
  const bytes = new Uint8Array(32);
  crypto.getRandomValues(bytes);
  return base64UrlEncode(bytes);
}

/**
 * SHA-256 hash → base64url (PKCE code_challenge).
 * Uses pure-JS SHA-256 — works on any origin, not just secure contexts.
 */
function computeCodeChallenge(verifier: string): string {
  const encoder = new TextEncoder();
  const digest = sha256(encoder.encode(verifier));
  return base64UrlEncode(digest);
}

function base64UrlEncode(buffer: Uint8Array): string {
  let binary = '';
  for (let i = 0; i < buffer.length; i++) {
    binary += String.fromCharCode(buffer[i]!);
  }
  return btoa(binary).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
}

// ── Config cache ────────────────────────────────────────────────────────────

interface GithubOAuthConfig {
  clientId: string;
  redirectUri: string;
  state: string;
}

/** Fetch OAuth config + state token from the backend. */
async function fetchGithubConfig(): Promise<GithubOAuthConfig | null> {
  try {
    const resp = await fetch('/api/ui/auth/github/config');
    if (!resp.ok) return null;
    return resp.json();
  } catch {
    return null;
  }
}

// ── Simple QQ icon ──────────────────────────────────────────────────────────

function QQIcon({ size = 20 }: { size?: number }) {
  return (
    <svg width={size} height={size} viewBox="0 0 24 24" fill="currentColor" aria-hidden="true">
      <path d="M12.003 2c-2.265 0-6.29 1.364-6.29 7.325v1.195S3.55 14.96 3.55 17.474c0 .665.17 1.025.567 1.41.724.706 1.645.73 1.645.73h.083c.294 0 .56-.037.793-.09-.035.174-.055.352-.055.537 0 1.193.942 2.693 2.398 2.971.194.037.392.058.595.058.624 0 1.226-.174 1.666-.466.37.21.846.349 1.358.376h.002c.512-.027.988-.166 1.358-.376.44.292 1.042.466 1.666.466.203 0 .4-.02.595-.058 1.456-.278 2.398-1.778 2.398-2.97 0-.186-.02-.364-.055-.538.233.053.499.09.793.09h.083s.921-.024 1.645-.73c.397-.385.567-.745.567-1.41 0-2.514-2.163-6.954-2.163-6.954V9.325C18.293 3.364 14.268 2 12.003 2z" />
    </svg>
  );
}

// ── Login page ──────────────────────────────────────────────────────────────

export default function LoginPage() {
  const { isAuthenticated, isLoading } = useAuth();
  const [searchParams] = useSearchParams();

  if (!isLoading && isAuthenticated) {
    return <Navigate to="/" replace />;
  }

  const error = searchParams.get('error');

  if (isLoading) {
    return (
      <div className="flex items-center justify-center h-screen bg-[#0f0d1e]">
        <p className="text-gray-400">加载中…</p>
      </div>
    );
  }

  /** GitHub: PKCE flow — browser generates challenge, redirects to GitHub. */
  const handleGithubLogin = async () => {
    try {
      const config = await fetchGithubConfig();
      if (!config) {
        window.location.href = '/auth/callback?error=server_error&provider=github';
        return;
      }

      // Generate PKCE parameters
      const codeVerifier = generateCodeVerifier();
      const codeChallenge = computeCodeChallenge(codeVerifier);

      // Store verifier + config in sessionStorage so AuthCallback can retrieve it
      sessionStorage.setItem('github_code_verifier', codeVerifier);
      sessionStorage.setItem('github_state', config.state);
      sessionStorage.setItem('github_client_id', config.clientId);
      sessionStorage.setItem('github_redirect_uri', config.redirectUri);

      // Build GitHub authorize URL
      const params = new URLSearchParams({
        client_id: config.clientId,
        redirect_uri: config.redirectUri,
        scope: 'read:user',
        state: config.state,
        code_challenge: codeChallenge,
        code_challenge_method: 'S256',
      });

      window.location.href =
        `https://github.com/login/oauth/authorize?${params.toString()}`;
    } catch {
      // Crypto API may throw in non-secure or unsupported contexts
      window.location.href = '/auth/callback?error=server_error&provider=github';
    }
  };

  return (
    <div className="flex items-center justify-center h-screen bg-[#0f0d1e]">
      <div className="w-full max-w-sm">
        <div className="text-center mb-8">
          <h1 className="text-2xl font-bold text-indigo-400">绯英管理</h1>
          <p className="text-sm text-gray-500 mt-1">aestival dashboard</p>
        </div>

        <div className="bg-gray-900 border border-gray-800 rounded-lg p-6 space-y-3">
          <p className="text-sm text-gray-400 text-center mb-4">
            选择登录方式
          </p>

          <button
            onClick={handleGithubLogin}
            className="flex items-center justify-center gap-3 w-full px-4 py-2.5
                       bg-gray-800 hover:bg-gray-700 border border-gray-700
                       rounded text-gray-200 transition-colors text-sm font-medium"
          >
            <Github size={20} aria-hidden="true" />
            使用 GitHub 登录
          </button>

          <a
            href="/api/ui/auth/qq"
            className="flex items-center justify-center gap-3 w-full px-4 py-2.5
                       bg-blue-900/30 hover:bg-blue-900/50 border border-blue-800/50
                       rounded text-blue-300 transition-colors text-sm font-medium
                       no-underline"
          >
            <QQIcon size={20} />
            使用 QQ 登录
          </a>

          <p className="text-xs text-gray-600 text-center pt-2">
            登录后可关联多个平台账号
          </p>
        </div>

        {error && (
          <div className="mt-4 p-3 bg-red-900/30 border border-red-800/50 rounded text-sm text-red-400">
            登录失败，请重试。
          </div>
        )}
      </div>
    </div>
  );
}
