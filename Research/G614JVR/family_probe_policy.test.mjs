import {test} from 'node:test';
import assert from 'node:assert/strict';
import {IDENTITY,MockTransport,probeMock} from './family_probe_policy.mjs';
test('valid even and odd family pairs perform exactly four mocked reads',()=>{
 const t=new MockTransport({'0:0':0x35a40,'0:4':0xb2,'1:0':0x35b40,'1:4':0xb1});
 assert.equal(probeMock(t).status,'MOCK_FAMILY_SANITY_PASS');
 assert.deepEqual(t.events,[[0,0],[0,4],[1,0],[1,4]]);
 assert.equal(IDENTITY.uniqueFromDeviceId,false);
});
test('a transfer error stops immediately without touching the other endpoint',()=>{
 const t=new MockTransport({'0:0':new Error('timeout')});
 assert.equal(probeMock(t).status,'STOP_TRANSFER_ERROR');
 assert.deepEqual(t.events,[[0,0]]);
});
test('mismatched family pair stops without retry or next endpoint',()=>{
 const t=new MockTransport({'0:0':0x35b40,'0:4':0xb2});
 assert.equal(probeMock(t).status,'STOP_FAMILY_MISMATCH');
 assert.deepEqual(t.events,[[0,0],[0,4]]);
});
test('unknown or live transports are rejected',()=>assert.throws(()=>probeMock({read(){throw Error('must not run');}}),/MOCK_ONLY/));
