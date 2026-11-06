import { applyCommand, applyMessage, encodeCommand, fromHex, parseState, parseTopic, toHex, type Bulbs } from './bulb';

const FW_STATE = '{"state":"ON","brightness":128,"color_mode":"rgb","color":{"r":255,"g":180,"b":100}}';

describe('parseTopic', () => {
  it('splits bulb topics', () => {
    expect(parseTopic('pico/bulb/living-room/state')).toEqual({ id: 'living-room', leaf: 'state' });
  });
  it('rejects other topics', () => {
    expect(parseTopic('pico/setup-test')).toBeNull();
    expect(parseTopic('other/bulb/x/state')).toBeNull();
    expect(parseTopic('pico/bulb/x/state/extra')).toBeNull();
    expect(parseTopic('pico/bulb//state')).toBeNull();
  });
});

describe('parseState', () => {
  it('reads the firmware state JSON', () => {
    expect(parseState(FW_STATE)).toEqual({ on: true, brightness: 128, color: { r: 255, g: 180, b: 100 } });
  });
  it('reads OFF and clamps values', () => {
    expect(parseState('{"state":"OFF","brightness":999,"color":{"r":-5,"g":300,"b":10}}'))
      .toEqual({ on: false, brightness: 255, color: { r: 0, g: 255, b: 10 } });
  });
  it('rejects garbage', () => {
    expect(parseState('not json')).toBeNull();
    expect(parseState('{"brightness":3}')).toBeNull();
    expect(parseState('null')).toBeNull();
  });
});

describe('encodeCommand', () => {
  it('uses the HA JSON schema field names', () => {
    expect(JSON.parse(encodeCommand({ on: true, brightness: 100, color: { r: 1, g: 2, b: 3 }, transition: 0.4 })))
      .toEqual({ state: 'ON', brightness: 100, color: { r: 1, g: 2, b: 3 }, transition: 0.4 });
  });
  it('only sends what was given, and never brightness 0 (firmware treats 0 as OFF)', () => {
    expect(JSON.parse(encodeCommand({ brightness: 0 }))).toEqual({ brightness: 1 });
    expect(JSON.parse(encodeCommand({ on: false }))).toEqual({ state: 'OFF' });
  });
});

describe('applyMessage', () => {
  it('builds a bulb from retained messages in any order', () => {
    let b: Bulbs = {};
    b = applyMessage(b, 'pico/bulb/lamp/state', FW_STATE);
    b = applyMessage(b, 'pico/bulb/lamp/status', 'online');
    b = applyMessage(b, 'pico/bulb/lamp/motion', '{"motion":true}');
    expect(b.lamp).toEqual({
      id: 'lamp', online: true, motion: true, schedule: null,
      state: { on: true, brightness: 128, color: { r: 255, g: 180, b: 100 } },
    });
  });
  it('stores the schedule the firmware reports', () => {
    const payload = '{"time_synced":true,"timer":{"ends":1800000000,"fade":60},"alarm":{"enabled":true,"hour":6,"minute":30,"days":[1,2,3,4,5],"rise":600,"brightness":255,"color":{"r":255,"g":180,"b":100}}}';
    expect(applyMessage({}, 'pico/bulb/lamp/schedule', payload).lamp.schedule).toEqual({
      timeSynced: true,
      timer: { ends: 1800000000, fade: 60 },
      alarm: { enabled: true, hour: 6, minute: 30, days: [1, 2, 3, 4, 5], rise: 600, brightness: 255, color: { r: 255, g: 180, b: 100 } },
    });
  });
  it('reports no timer when the firmware sends null, and ignores bad schedules', () => {
    const payload = '{"time_synced":false,"timer":null,"alarm":{"enabled":false,"hour":7,"minute":0,"days":[],"rise":1800,"brightness":255,"color":{"r":255,"g":180,"b":100}}}';
    const b = applyMessage({}, 'pico/bulb/lamp/schedule', payload);
    expect(b.lamp.schedule?.timer).toBeNull();
    expect(b.lamp.schedule?.timeSynced).toBe(false);
    expect(applyMessage(b, 'pico/bulb/lamp/schedule', 'garbage')).toBe(b);
    expect(applyMessage(b, 'pico/bulb/lamp/schedule', '{}')).toBe(b);
  });
  it('marks a bulb offline from its Last Will', () => {
    const b = applyMessage({}, 'pico/bulb/lamp/status', 'online');
    expect(applyMessage(b, 'pico/bulb/lamp/status', 'offline').lamp.online).toBe(false);
  });
  it('removes a bulb when its retained status is cleared', () => {
    const b = applyMessage({}, 'pico/bulb/lamp/status', 'online');
    expect(applyMessage(b, 'pico/bulb/lamp/status', '')).toEqual({});
  });
  it('ignores unrelated topics and bad payloads without copying', () => {
    const b = applyMessage({}, 'pico/bulb/lamp/status', 'online');
    expect(applyMessage(b, 'pico/setup-test', 'ok')).toBe(b);
    expect(applyMessage(b, 'pico/bulb/lamp/state', 'garbage')).toBe(b);
  });
});

describe('applyCommand (optimistic UI)', () => {
  const s = { on: false, brightness: 50, color: { r: 1, g: 2, b: 3 } };
  it('changes colour/brightness without touching power, like the firmware', () => {
    expect(applyCommand(s, { color: { r: 9, g: 9, b: 9 } })).toEqual({ ...s, color: { r: 9, g: 9, b: 9 } });
    expect(applyCommand(s, { on: true, brightness: 200 })).toEqual({ ...s, on: true, brightness: 200 });
  });
  it('a power command keeps colour and brightness', () => {
    expect(applyCommand({ ...s, on: true }, { on: false })).toEqual(s);
  });
});

describe('hex', () => {
  it('round-trips', () => {
    expect(toHex({ r: 255, g: 128, b: 0 })).toBe('#ff8000');
    expect(fromHex('#FF8000')).toEqual({ r: 255, g: 128, b: 0 });
    expect(fromHex('nope')).toBeNull();
  });
});
