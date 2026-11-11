"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.YMVoice = void 0;
class YMVoice {
    __type;
    constructor(__type) {
        this.__type = __type;
    }
    toHash() {
        return this.encode()
            .map((e) => ("0" + e.toString(16)).slice(-2))
            .join("");
    }
}
exports.YMVoice = YMVoice;
