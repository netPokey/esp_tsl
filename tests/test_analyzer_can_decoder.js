"use strict";

const test = require("node:test");
const assert = require("node:assert/strict");
const {
  decodeCanRecord,
  decodeSignal,
  extractIntelBits,
} = require("../data/analyzer/can_decoder.js");

function frame(id, data, dlc = data.length) {
  return { channel: 0, id, dlc, data };
}

function signal(result, name) {
  return result.signals.find((item) => item.name === name);
}

test("Intel extractor handles unsigned fields crossing bytes", () => {
  assert.equal(extractIntelBits([0x80, 0x05], 7, 4, false), 11);
});

test("Intel extractor handles signed fields without 32-bit truncation", () => {
  assert.equal(extractIntelBits([0x00, 0x00, 0x00, 0x00, 0xFE, 0xFF], 32, 16, true), -2);
  assert.equal(extractIntelBits([0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01], 0, 41, false), 0x1FFFFFFFFFF);
  assert.throws(() => extractIntelBits(new Uint8Array(8).fill(0xFF), 0, 64, false), /safe range/);
});

test("generic signal decoder applies signed scaling and enum labels", () => {
  assert.deepEqual(
    decodeSignal([0x0E], {
      name: "scaledSigned",
      start: 0,
      length: 4,
      byte_order: "intel",
      signed: true,
      factor: 0.5,
      offset: 2,
      enum: { "-2": "NEGATIVE_TWO" },
    }),
    { name: "scaledSigned", raw: -2, value: 1, enumLabel: "NEGATIVE_TWO" },
  );
});

test("0x107 decodes four exact IBST helper rules with safe minimum DLC 5", () => {
  const data = new Uint8Array(5);
  const packed = (5n << 12n) | (2n << 16n) | (3n << 18n) | (0x140n << 21n);
  for (let index = 0; index < data.length; index += 1) {
    data[index] = Number((packed >> BigInt(index * 8)) & 0xFFn);
  }

  const decoded = decodeCanRecord(frame(0x107, data));
  assert.equal(decoded.status, "decoded");
  assert.match(decoded.note, /can_helpers\.h/);
  assert.deepEqual(signal(decoded, "IBST_iBoosterStatus"), {
    name: "IBST_iBoosterStatus", raw: 5, value: 5, enumLabel: "READY",
  });
  assert.equal(signal(decoded, "IBST_driverBrakeApply").enumLabel, "DRIVER_APPLYING_BRAKES");
  assert.equal(signal(decoded, "IBST_internalState").enumLabel, "EXTERNAL_BRAKE_REQUEST");
  assert.equal(signal(decoded, "IBST_sInputRodDriver").value, 0);
  assert.equal(decodeCanRecord(frame(0x107, data.slice(0, 4))).status, "invalid_dlc");
});

test("0x212 follows can_helpers bit 29 charge request and physical scaling", () => {
  const data = new Uint8Array(8);
  const packed = (1n << 5n) | (1n << 6n) | (1n << 7n) |
    (2n << 14n) | (4n << 16n) | (100n << 19n) |
    (1n << 29n) | (3n << 32n) | (80n << 40n) |
    (5n << 51n) | (6n << 56n);
  for (let index = 0; index < data.length; index += 1) {
    data[index] = Number((packed >> BigInt(index * 8)) & 0xFFn);
  }

  const decoded = decodeCanRecord(frame(0x212, data));
  assert.equal(decoded.status, "decoded");
  assert.equal(signal(decoded, "BMS_activeHeatingWorthwhile").raw, 1);
  assert.equal(signal(decoded, "BMS_cpMiaOnHvs").raw, 1);
  assert.equal(signal(decoded, "BMS_pcsPwmEnabled").raw, 1);
  assert.equal(signal(decoded, "BMS_ecuLogUploadRequest").raw, 2);
  assert.equal(signal(decoded, "BMS_hvState").raw, 4);
  assert.equal(signal(decoded, "BMS_isolationResistance").value, 1000);
  assert.equal(signal(decoded, "BMS_chargeRequest").raw, 1);
  assert.equal(signal(decoded, "BMS_keepWarmRequest").raw, 0);
  assert.equal(signal(decoded, "BMS_bmsState").enumLabel, "CHARGE");
  assert.equal(signal(decoded, "BMS_chgPowerAvailable").value, 10);
  assert.equal(signal(decoded, "BMS_chargeRetryCount").raw, 5);
  assert.equal(signal(decoded, "BMS_smStateRequest").raw, 6);
});

test("0x389 helper uses bit 9 as the tenth speed bit and DLC 7", () => {
  const data = new Uint8Array(7);
  data[1] = 0x02;
  data[3] = 0x80;
  data[5] = 128;
  const decoded = decodeCanRecord(frame(0x389, data));
  assert.equal(decoded.status, "decoded");
  assert.equal(signal(decoded, "DAS_accSpeedLimit").raw, 512);
  assert.equal(signal(decoded, "DAS_accSpeedLimit").value, 204.8);
  assert.equal(signal(decoded, "DAS_lssState").raw, 1);
  assert.deepEqual(signal(decoded, "DAS_ppOffsetDesiredRamp"), {
    name: "DAS_ppOffsetDesiredRamp", raw: 128, value: 0,
  });
  assert.equal(decodeCanRecord(frame(0x389, data.slice(0, 6))).status, "invalid_dlc");
});

test("0x3E2 uses DBC brakeLightStatus values 0 through 2", () => {
  assert.equal(signal(decodeCanRecord(frame(0x3E2, [0, 0, 0, 0, 0, 0, 0, 0])), "VCLEFT_brakeLightStatus").enumLabel, "OFF");
  assert.equal(signal(decodeCanRecord(frame(0x3E2, [2, 0, 0, 0, 0, 0, 0, 0])), "VCLEFT_brakeLightStatus").enumLabel, "FAULT");
});

test("fw9 0x257 rule is Intel start 24 length 9 with minimum DLC 5", () => {
  const decoded = decodeCanRecord(frame(0x257, [0, 0, 0, 0x2C, 0x01]));
  assert.deepEqual(signal(decoded, "DI_uiSpeed"), { name: "DI_uiSpeed", raw: 300, value: 300 });
  assert.equal(decodeCanRecord(frame(0x257, [0, 0, 0, 0x2C])).status, "invalid_dlc");
});

test("fw9 door and HVAC frames decode multiple fields", () => {
  const leftDoors = decodeCanRecord(frame(0x102, [0x03]));
  assert.equal(leftDoors.status, "decoded");
  assert.equal(signal(leftDoors, "VCLEFT_doorFL").raw, 1);
  assert.equal(signal(leftDoors, "VCLEFT_doorRL").raw, 1);

  const rightDoors = decodeCanRecord(frame(0x103, [0x02]));
  assert.equal(signal(rightDoors, "VCRIGHT_doorFR").raw, 0);
  assert.equal(signal(rightDoors, "VCRIGHT_doorRR").raw, 1);

  const hvac = decodeCanRecord(frame(0x20C, [0x34, 0x05, 0, 0, 0xAB, 0x02]));
  assert.equal(hvac.status, "decoded");
  assert.equal(signal(hvac, "VCRIGHT_hvacBlowerRaw").raw, 0x534);
  assert.equal(signal(hvac, "VCRIGHT_hvacF2Raw").raw, 0x2AB);
  assert.equal(decodeCanRecord(frame(0x20C, [0x34, 0x05, 0, 0, 0xAB])).status, "invalid_dlc");
});

test("0x33A exposes only gated-safe SOC at minimum DLC 4", () => {
  const decoded = decodeCanRecord(frame(0x33A, [0x55, 0x02, 0xA0, 0x05]));
  assert.equal(decoded.status, "decoded");
  assert.equal(signal(decoded, "UI_soc").raw, 0x5A);
  assert.equal(signal(decoded, "UI_range"), undefined);
  assert.equal(decoded.signals.some((item) => /range/i.test(item.name)), false);
  assert.match(decoded.note, /D5 != 0xFF/);
  assert.equal(decodeCanRecord(frame(0x33A, [0x55, 0x02, 0xA0])).status, "invalid_dlc");
});

test("fw9 high-byte raw fields enforce their actual minimum DLC", () => {
  const ambient = decodeCanRecord(frame(0x321, [0, 0, 0, 0, 0, 0x7B]));
  assert.equal(signal(ambient, "VCFRONT_ambientRaw").raw, 0x7B);
  assert.equal(decodeCanRecord(frame(0x321, [0, 0, 0, 0, 0])).status, "invalid_dlc");

  const control = decodeCanRecord(frame(0x21C, [0, 0, 0, 0, 0, 0, 0xA5]));
  assert.equal(signal(control, "Control_D6Raw").raw, 0xA5);
  assert.equal(decodeCanRecord(frame(0x21C, new Uint8Array(6))).status, "invalid_dlc");

  const lighting = decodeCanRecord(frame(0x679, [0, 0, 0, 0, 0, 0xC3]));
  assert.equal(signal(lighting, "UI_ambientLightingD5Raw").raw, 0xC3);
  assert.equal(decodeCanRecord(frame(0x679, new Uint8Array(5))).status, "invalid_dlc");
});

test("catalog status classification, conflicts, and mux listing are stable", () => {
  const decoded = decodeCanRecord(frame(0x118, new Uint8Array(3)));
  assert.equal(decoded.status, "decoded");
  assert.match(decoded.note, /fw9/);
  const conflict = decodeCanRecord(frame(0x25D, new Uint8Array(8)));
  assert.equal(conflict.status, "unsupported_conflict");
  assert.match(conflict.note, /冲突/);
  const mux = decodeCanRecord(frame(0x3C2, new Uint8Array(8)));
  assert.equal(mux.status, "listed");
  assert.equal(mux.signals, undefined);
  assert.equal(decodeCanRecord(frame(0x212, new Uint8Array(7))).status, "invalid_dlc");
  assert.equal(decodeCanRecord(frame(0x001, new Uint8Array(8))).status, "unknown");
});

test("supported catalog IDs classify short data as invalid_dlc", () => {
  assert.equal(decodeCanRecord(frame(0x212, new Uint8Array(8), 7)).status, "invalid_dlc");
  assert.equal(decodeCanRecord(frame(0x212, new Uint8Array(2), 8)).status, "invalid_dlc");
  assert.equal(decodeCanRecord(frame(0x212, new Uint8Array(2), 2)).status, "invalid_dlc");
});

test("bad IDs and malformed records never throw and remain unknown", () => {
  const badRecords = [
    null,
    {},
    { id: -1, dlc: 0, data: [] },
    { id: 0x800, dlc: 8, data: new Uint8Array(8) },
    { id: 0x212, dlc: -1, data: [] },
    { id: 0x212, dlc: 9, data: new Uint8Array(9) },
    { id: 0x212, dlc: 1, data: "00" },
    { id: 0x212, dlc: 1, data: [256] },
  ];
  for (const record of badRecords) {
    assert.doesNotThrow(() => decodeCanRecord(record));
    assert.equal(decodeCanRecord(record).status, "unknown");
  }
});
