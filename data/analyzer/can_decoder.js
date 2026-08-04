(function(root, factory) {
  var api = factory(
    root && root.ANALYZER_CAN_CATALOG,
    typeof module === "object" && module.exports ? require("./can_catalog.js") : null
  );
  if (root) root.ANALYZER_CAN_DECODER = api;
  if (typeof module === "object" && module.exports) module.exports = api;
})(typeof globalThis !== "undefined" ? globalThis : this, function(browserCatalog, commonJsCatalog) {
  "use strict";

  var catalog = browserCatalog || commonJsCatalog;
  var messagesById = Object.create(null);

  if (catalog && Array.isArray(catalog.messages)) {
    catalog.messages.forEach(function(message) {
      messagesById[message.id] = message;
    });
  }

  function isByteArray(data) {
    return Array.isArray(data) ||
      (typeof Uint8Array !== "undefined" && data instanceof Uint8Array);
  }

  function validateBytes(data) {
    if (!isByteArray(data)) throw new TypeError("data must be an Array or Uint8Array");
    for (var i = 0; i < data.length; i += 1) {
      if (!Number.isInteger(data[i]) || data[i] < 0 || data[i] > 255) {
        throw new RangeError("data bytes must be integers in 0..255");
      }
    }
  }

  // Intel signals number bit 0 as the least-significant bit of data[0].
  // BigInt avoids all JS 32-bit bitwise truncation. The public result remains a
  // Number and is rejected if the decoded integer cannot be represented safely.
  function extractIntelBits(data, start, length, signed) {
    validateBytes(data);
    if (!Number.isInteger(start) || start < 0) throw new RangeError("start must be a non-negative integer");
    if (!Number.isInteger(length) || length < 1 || length > 64) throw new RangeError("length must be in 1..64");
    if (start + length > data.length * 8) throw new RangeError("signal exceeds available data");

    var raw = 0n;
    for (var bit = 0; bit < length; bit += 1) {
      var absoluteBit = start + bit;
      var byteValue = data[Math.floor(absoluteBit / 8)];
      if ((byteValue & (1 << (absoluteBit % 8))) !== 0) raw |= 1n << BigInt(bit);
    }

    if (signed && (raw & (1n << BigInt(length - 1))) !== 0n) {
      raw -= 1n << BigInt(length);
    }
    var value = Number(raw);
    if (!Number.isSafeInteger(value)) throw new RangeError("decoded integer exceeds Number safe range");
    return value;
  }

  function decodeSignal(data, signal) {
    if (!signal || signal.byte_order !== "intel") throw new TypeError("unsupported signal definition");
    var raw = extractIntelBits(data, signal.start, signal.length, signal.signed === true);
    var factor = Number(signal.factor);
    var offset = Number(signal.offset);
    if (!Number.isFinite(factor) || !Number.isFinite(offset)) throw new TypeError("invalid signal scaling");
    var value = raw * factor + offset;
    if (!Number.isFinite(value)) throw new RangeError("decoded value is not finite");

    var decoded = { name: signal.name, raw: raw, value: value };
    var enumMap = signal.enum;
    if (enumMap && Object.prototype.hasOwnProperty.call(enumMap, String(raw))) {
      decoded.enumLabel = enumMap[String(raw)];
    }
    return decoded;
  }

  function decodeCanRecord(record) {
    try {
      if (!record || typeof record !== "object") return { status: "unknown" };
      var id = record.id;
      var dlc = record.dlc;
      var data = record.data;
      if (!Number.isInteger(id) || id < 0 || id > 0x7FF) return { status: "unknown" };
      if (!Number.isInteger(dlc) || dlc < 0 || dlc > 8) return { status: "unknown" };

      var message = messagesById[id];
      if (!message) return { status: "unknown" };
      var result = { status: message.status, message: message.hex, name: message.name };
      if (message.note) result.note = message.note;

      validateBytes(data);
      if (message.status === "unsupported_conflict" || message.status === "listed") {
        if (data.length < dlc) return { status: "unknown" };
        return result;
      }
      if (message.status !== "supported" || !Array.isArray(message.signals)) return { status: "unknown" };

      var requiredBytes = message.signals.reduce(function(maximum, signal) {
        return Math.max(maximum, Math.ceil((signal.start + signal.length) / 8));
      }, 0);
      if (dlc < message.dlc || dlc < requiredBytes || data.length < dlc || data.length < requiredBytes) {
        result.status = "invalid_dlc";
        return result;
      }

      var frameData = Array.prototype.slice.call(data, 0, dlc);
      result.status = "decoded";
      result.signals = message.signals.map(function(signal) {
        return decodeSignal(frameData, signal);
      });
      return result;
    } catch (error) {
      return { status: "unknown" };
    }
  }

  return {
    decodeCanRecord: decodeCanRecord,
    decodeSignal: decodeSignal,
    extractIntelBits: extractIntelBits
  };
});
