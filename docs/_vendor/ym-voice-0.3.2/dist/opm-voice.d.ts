import { OPNVoice, OPNSlotParam } from "./opn-voice";
import { YMVoice } from "./ym-voice";
export declare class OPMSlotParam {
    __type: "OPMSlotParam";
    dt1: number;
    ml: number;
    tl: number;
    ks: number;
    ar: number;
    am: number;
    dt2: number;
    dr: number;
    sr: number;
    sl: number;
    rr: number;
    constructor(init?: Partial<OPMSlotParam>);
    toOPN(): OPNSlotParam;
}
export type OPMVoiceObject = {
    fb: number;
    con: number;
    ams: number;
    pms: number;
    slots: [Partial<OPMSlotParam>, Partial<OPMSlotParam>, Partial<OPMSlotParam>, Partial<OPMSlotParam>];
};
export declare class OPMVoice extends YMVoice {
    fb: number;
    con: number;
    ams: number;
    pms: number;
    slots: [OPMSlotParam, OPMSlotParam, OPMSlotParam, OPMSlotParam];
    constructor(init?: OPMVoiceObject);
    /**
     *     |D7|D6|D5|D4|D3|D2|D1|D0|
     * 00: |--|  DT1   |     ML    | # slot1 (M1)
     * 01: |--|  DT1   |     ML    | # slot3 (M2)
     * 02: |--|  DT1   |     ML    | # slot2 (C1)
     * 03: |--|  DT1   |     ML    | # slot4 (C2)
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
     * 10: | DT2 |--|      SR      | # slot1 (M1)
     * 11: | DT2 |--|      SR      | # slot3 (M2)
     * 12: | DT2 |--|      SR      | # slot2 (C1)
     * 13: | DT2 |--|      SR      | # slot4 (C2)
     * 14: |     SL    |    RR     | # slot1 (M1)
     * 15: |     SL    |    RR     | # slot3 (M2)
     * 16: |     SL    |    RR     | # slot2 (C1)
     * 17: |     SL    |    RR     | # slot4 (C2)
     * 18: |-----|   FB   |  CON   |
     * 19: |-----|  PMS   |--| AMS |
     *     |D7|D6|D5|D4|D3|D2|D1|D0|
     */
    static decode(d: ArrayLike<number>): OPMVoice;
    encode(): Array<number>;
    static fromJSON(str: string): OPMVoice;
    toOPN(): OPNVoice;
    toMML(type?: "mxdrv" | "pmd"): string;
    toFile(type?: "opm" | "dmp" | "vgi" | "tfi", parameters?: {
        [key: string]: unknown;
    }): string | Uint8Array;
}
