import assert from 'node:assert/strict';
import * as c from './leeftrader-client.mjs';
let n = 0; const t = (name, fn) => { fn(); n++; console.log('ok', name); };
const W = (q) => ({ quantity: q, contract: 'eosio.token' });
t('default account', () => assert.equal(c.CONTRACT_ACCOUNT, process.env.CONTRACT_ACCOUNT || 'smartatrader'));
t('banned actors rejected', () => c.BANNED.forEach((a) => assert.throws(() => c.pauseAction({ actor: a }))));
t('buyback payload', () => { const a = c.buybackAction({ actor: 'botacct', bucket: 'satlp', inAsset: W('1.00000000 WAX'), poolPath: '1252', minOut: { quantity: '10.0000 SAT', contract: 'sattokenacct' } });
  assert.equal(a.authorization[0].permission, 'bot'); assert.equal(a.data.pool_path, '1252'); });
t('memo injection rejected', () => assert.throws(() => c.buybackAction({ actor: 'botacct', bucket: 'satlp', inAsset: W('1.00000000 WAX'), poolPath: '1#evil', minOut: W('2.00000000 WAX') })));
t('loop +1 unit rule', () => { assert.throws(() => c.buybackAction({ actor: 'botacct', bucket: 'x', inAsset: W('1.00000000 WAX'), poolPath: '1,2', minOut: W('1.00000000 WAX') }));
  c.buybackAction({ actor: 'botacct', bucket: 'x', inAsset: W('1.00000000 WAX'), poolPath: '1,2', minOut: W('1.00000001 WAX') }); });
t('addlp ABI field names', () => {
  const a = c.addlpAction({ actor: 'botacct', bucket: 'satlp', poolId: 12405,
    a: W('1.00000000 WAX'), b: { quantity: '1.0000 SAT', contract: 'sattoken1111' },
    tickLower: -105960, tickUpper: -93960, minA: '1.00000000 WAX', minB: '1.0000 SAT' });
  assert.equal(a.data.tick_lower, -105960);
  assert.equal(a.data.tick_upper, -93960);
  assert.equal('tl' in a.data, false);
  assert.equal('tu' in a.data, false);
});
t('drawdown 10% pauses', () => { assert.equal(c.drawdownGuard({ actor: 'botacct', peak: 100, equity: 91 }), null); assert.equal(c.drawdownGuard({ actor: 'botacct', peak: 100, equity: 90 }).name, 'pause'); });
console.log(`RESULT: ${n} passed`);
