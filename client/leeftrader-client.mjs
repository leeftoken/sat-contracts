// Bot-side client for the leeftrader contract. Builds UNSIGNED action payloads
// and reads tables. It never loads, holds or prints keys: signing happens in the
// bot's existing signer with the dedicated `leeftrader.bot` permission only.
// Contract account: CONTRACT_ACCOUNT env, default "smartatrader".
export const CONTRACT_ACCOUNT = process.env.CONTRACT_ACCOUNT || 'smartatrader';
// Bot actions only. Admin actions go through eosio.msig (2-of-3), not this client.
export const BANNED = ['mywaxgallery', 'danishcrypto', 'qu.ui.wam', 'skgaw.wam', 'dustwizard42', 'niftywarlord', 'smart.ass'];
const NAME = /^[a-z1-5.]{1,12}[a-j1-5]?$/;

export function assertActor(a) {
  if (!NAME.test(a)) throw new Error(`bad account name ${a}`);
  if (BANNED.includes(a)) throw new Error(`trading account ${a} may not act on the contract`);
}
const auth = (actor, permission = 'bot') => { assertActor(actor); return [{ actor, permission }]; };

export function unitsOf(q) { const [n] = q.split(' '); const d = (n.split('.')[1] || '').length; return { raw: BigInt(n.replace('.', '')), d }; }

export function buybackAction({ actor, bucket, inAsset, swap = 'swap.alcor', poolPath, minOut }) {
  if (!/^[0-9]+(,[0-9]+)*$/.test(poolPath)) throw new Error('pool path must be digits/commas');
  if (inAsset.contract === minOut.contract && inAsset.quantity.split(' ')[1] === minOut.quantity.split(' ')[1]) {
    const a = unitsOf(inAsset.quantity), b = unitsOf(minOut.quantity);
    if (b.raw < a.raw + 1n) throw new Error('same-token loop: min_out must be >= in + 1 unit');
  }
  return { account: CONTRACT_ACCOUNT, name: 'buyback', authorization: auth(actor),
    data: { bucket, in: inAsset, swap, pool_path: poolPath, min_out: minOut } };
}
export function addlpAction({ actor, bucket, poolId, a, b, tickLower, tickUpper, minA, minB }) {
  return { account: CONTRACT_ACCOUNT, name: 'addlp', authorization: auth(actor),
    data: { bucket, pool_id: poolId, a, b, tl: tickLower, tu: tickUpper, min_a: minA, min_b: minB } };
}
export function collectfeesAction({ actor, lpId, maxA, maxB }) {
  return { account: CONTRACT_ACCOUNT, name: 'collectfees', authorization: auth(actor),
    data: { actor, lp_id: lpId, max_a: maxA, max_b: maxB } };
}
export function pauseAction({ actor }) {
  return { account: CONTRACT_ACCOUNT, name: 'pause', authorization: auth(actor), data: { actor, paused: true } };
}
/** 10% drawdown stop: returns a pause action when equity fell >= 10% from peak. */
export function drawdownGuard({ actor, peak, equity, limit = 0.10 }) {
  if (!(peak > 0)) return null;
  return (peak - equity) / peak >= limit ? pauseAction({ actor }) : null;
}

export async function table(rpc, tbl, scope = CONTRACT_ACCOUNT, extra = {}) {
  const r = await fetch(`${rpc}/v1/chain/get_table_rows`, { method: 'POST', headers: { 'content-type': 'application/json' },
    body: JSON.stringify({ json: true, code: CONTRACT_ACCOUNT, scope, table: tbl, limit: 100, ...extra }) });
  if (!r.ok) throw new Error(`rpc ${r.status}`);
  return (await r.json()).rows;
}
/** Hyperion history of contract log events. */
export async function logs(hyperion, names = ['logrevenue', 'logsub', 'logswap', 'loglp'], limit = 50) {
  const f = names.map((n) => `${CONTRACT_ACCOUNT}:${n}`).join(',');
  const r = await fetch(`${hyperion}/v2/history/get_actions?filter=${encodeURIComponent(f)}&limit=${limit}&sort=desc`);
  if (!r.ok) throw new Error(`hyperion ${r.status}`);
  return (await r.json()).actions;
}
