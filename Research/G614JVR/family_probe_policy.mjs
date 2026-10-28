// Offline policy model only. No OS device access, controller or kext integration.
export const IDENTITY = Object.freeze({family:'CS35L51', confidence:'HIGH', method:'OEM_PLATFORM_EVIDENCE', uniqueFromDeviceId:false});
const values = new WeakMap();
export class MockTransport {
  constructor(fixtures) { values.set(this, {fixtures, events: []}); }
  read(cs, address) {
    const state = values.get(this);
    state.events.push([cs, address]);
    const value = state.fixtures[`${cs}:${address}`];
    if (value instanceof Error) throw value;
    if (!Number.isInteger(value) || value < 0 || value > 0xffffffff) throw Error('INVALID_RESPONSE');
    return value;
  }
  get events() { return values.get(this).events.map(e => [...e]); }
}
export function probeMock(transport) {
  if (Object.getPrototypeOf(transport) !== MockTransport.prototype) throw Error('MOCK_ONLY');
  const result = [];
  for (const cs of [0, 1]) {
    let devid, revid;
    try { devid = transport.read(cs, 0); revid = transport.read(cs, 4); }
    catch { return {status:'STOP_TRANSFER_ERROR', result}; }
    const expected = (revid & 0xf) % 2 ? 0x35b40 : 0x35a40;
    result.push({cs,devid,revid,expected});
    if (devid !== expected) return {status:'STOP_FAMILY_MISMATCH', result};
  }
  return {status:'MOCK_FAMILY_SANITY_PASS', result};
}
