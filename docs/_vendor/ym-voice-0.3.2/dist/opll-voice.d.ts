import { OPNSlotParam, OPNVoice } from "./opn-voice";
import { OPMVoice } from "./opm-voice";
import { OPLSlotParam, OPLVoice } from "./opl-voice";
import { YMVoice } from "./ym-voice";
export declare class OPLLSlotParam {
    __type: "OPLLSlotParam";
    am: number;
    pm: number;
    eg: number;
    ml: number;
    kr: number;
    kl: number;
    tl: number;
    ar: number;
    dr: number;
    sl: number;
    rr: number;
    ws: number;
    constructor(init?: Partial<OPLLSlotParam>);
    toOPL(): OPLSlotParam;
    toOPN(car: boolean): OPNSlotParam;
}
export type OPLLVoiceObject = {
    fb: number;
    slots: [Partial<OPLLSlotParam>, Partial<OPLLSlotParam>];
};
export declare class OPLLVoice extends YMVoice {
    fb: number;
    slots: [OPLLSlotParam, OPLLSlotParam];
    constructor(init?: OPLLVoiceObject);
    /**
     *    |D7|D6|D5|D4|D3|D2|D1|D0|
     * 0: |AM|PM|EG|KR|    ML     | # slot1
     * 1: |AM|PM|EG|KR|    ML     | # slot2
     * 2: |  KL |        TL       | # slot1
     * 3: |  KL |--|WC|WM|   FB   | # slot2
     * 4: |     AR    |    DR     | # slot1
     * 5: |     AR    |    DR     | # slot2
     * 6: |     SL    |    RR     | # slot1
     * 7: |     SL    |    RR     | # slot2
     *    |D7|D6|D5|D4|D3|D2|D1|D0|
     *
     * WC/WM is waveform for slot#2 and slot#1, respectively.
     */
    encode(): Array<number>;
    static decode(d: ArrayLike<number>): OPLLVoice;
    static fromJSON(str: string): OPLLVoice;
    toOPN(): OPNVoice;
    toOPM(): OPMVoice;
    toOPL(): OPLVoice;
    toMML(_type?: "mgsdrv"): string;
    toFile(): string | Uint8Array;
}
export declare const OPLLVoiceMap: {
    ym2413: OPLLVoice[];
    vrc7: OPLLVoice[];
    ymf281b: OPLLVoice[];
};
export type OPLLVoiceVariant = keyof typeof OPLLVoiceMap;
export declare function getOPLLRomVoices(variant?: OPLLVoiceVariant | string | null): OPLLVoice[];
