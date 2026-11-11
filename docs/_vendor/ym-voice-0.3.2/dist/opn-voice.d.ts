import { OPMSlotParam, OPMVoice } from "./opm-voice";
import { OPLSlotParam, OPLVoice } from "./opl-voice";
import { YMVoice } from "./ym-voice";
export declare class OPNSlotParam {
    __type: "OPNSlotParam";
    dt: number;
    ml: number;
    tl: number;
    ks: number;
    ar: number;
    am: number;
    dr: number;
    sr: number;
    sl: number;
    rr: number;
    ssg: number;
    constructor(init?: Partial<OPNSlotParam>);
    toOPL(key: boolean): OPLSlotParam;
    toOPM(): OPMSlotParam;
}
export type OPNVoiceObject = {
    fb: number;
    con: number;
    ams: number;
    pms: number;
    slots: [Partial<OPNSlotParam>, Partial<OPNSlotParam>, Partial<OPNSlotParam>, Partial<OPNSlotParam>];
};
export declare class OPNVoice extends YMVoice {
    fb: number;
    con: number;
    ams: number;
    pms: number;
    slots: [OPNSlotParam, OPNSlotParam, OPNSlotParam, OPNSlotParam];
    constructor(init?: OPNVoiceObject);
    /**
     *     |D7|D6|D5|D4|D3|D2|D1|D0|
     * 00: |--|   DT   |     ML    | # slot1 (M1)
     * 01: |--|   DT   |     ML    | # slot3 (M2)
     * 02: |--|   DT   |     ML    | # slot2 (C1)
     * 03: |--|   DT   |     ML    | # slot4 (C2)
     * 04: |--|         TL         | # slot1 (M1)
     * 05: |--|         TL         | # slot3 (M2)
     * 06: |--|         TL         | # slot2 (C1)
     * 07: |--|         TL         | # slot4 (C2)
     * 08: |--------|      AR      | # slot1 (M1)
     * 09: |--------|      AR      | # slot3 (M2)
     * 0A: |--------|      AR      | # slot2 (C1)
     * 0B: |--------|      AR      | # slot4 (C2)
     * 0C: |AM|-----|      DR      | # slot1 (M1)
     * 0D: |AM|-----|      DR      | # slot3 (M2)
     * 0E: |AM|-----|      DR      | # slot2 (C1)
     * 0F: |AM|-----|      DR      | # slot4 (C2)
     * 10: |--------|      SR      | # slot1 (M1)
     * 11: |--------|      SR      | # slot3 (M2)
     * 12: |--------|      SR      | # slot2 (C1)
     * 13: |--------|      SR      | # slot4 (C2)
     * 14: |     SL    |    RR     | # slot1 (M1)
     * 15: |     SL    |    RR     | # slot3 (M2)
     * 16: |     SL    |    RR     | # slot2 (C1)
     * 17: |     SL    |    RR     | # slot4 (C2)
     * 18: |-----------|   SSG-EG  | # slot1 (M1)
     * 19: |-----------|   SSG-EG  | # slot3 (M2)
     * 1A: |-----------|   SSG-EG  | # slot2 (C1)
     * 1B: |-----------|   SSG-EG  | # slot4 (C2)
     * 1C: |-----|   FB   |  CON   |
     * 1D: |--|--| AMS |--|  PMS   |
     *     |D7|D6|D5|D4|D3|D2|D1|D0|
     */
    static decode(d: ArrayLike<number>): OPNVoice;
    encode(): Array<number>;
    static fromJSON(str: string): OPNVoice;
    toOPM(): OPMVoice;
    /**
     * @param keyOn: if true, the resulting parameter is for key-on. otherwise for key-off.
     * @returns OPL voice parameters for simulating OPN voice with dual OPL channels.
     * Use `toOPL()[0]` for single OPL channel.
     */
    toOPL(keyOn?: boolean): [OPLVoice, OPLVoice];
    toMML(type?: "mucom88" | "mucom88:poll_v" | "pmd"): string;
    toFile(type?: "dmp" | "tfi" | "vgi"): string | Uint8Array;
}
