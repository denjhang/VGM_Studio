import { OPLLVoice, OPLLSlotParam, OPLLVoiceVariant } from "./opll-voice";
import { OPNSlotParam } from "./opn-voice";
import { YMVoice } from "./ym-voice";
export declare class OPLSlotParam {
    __type: "OPLSlotParam";
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
    constructor(init?: Partial<OPLSlotParam>);
    toOPLL(): OPLLSlotParam;
    toOPN(car: boolean): OPNSlotParam;
}
export type OPLVoiceObject = {
    fb: number;
    con: number;
    slots: [Partial<OPLSlotParam>, Partial<OPLSlotParam>];
};
export declare class OPLVoice extends YMVoice {
    fb: number;
    con: number;
    slots: [OPLSlotParam, OPLSlotParam];
    constructor(init?: OPLVoiceObject);
    /**
     *    |D7|D6|D5|D4|D3|D2|D1|D0|
     * 0: |AM|PM|EG|KR|    ML     | # slot1
     * 1: |AM|PM|EG|KR|    ML     | # slot2
     * 2: |  KL |        TL       | # slot1
     * 3: |  KL |        TL       | # slot2
     * 4: |     AR    |    DR     | # slot1
     * 5: |     AR    |    DR     | # slot2
     * 6: |     SL    |    RR     | # slot1
     * 7: |     SL    |    RR     | # slot2
     * 8: |-----------------| WS  | # slot1
     * 9: |-----------------| WS  | # slot2
     * A: |-----------|   FB   |CN|
     *    |D7|D6|D5|D4|D3|D2|D1|D0|
     */
    encode(): Array<number>;
    static decode(d: ArrayLike<number>): OPLVoice;
    static fromJSON(str: string): OPLVoice;
    toOPLLVoice(): OPLLVoice;
    toOPLLROMVoice(variant?: OPLLVoiceVariant): {
        program: number;
        volumeOffset: number;
        octaveOffset: number;
    };
    toMML(_type?: "pmd"): string;
    toFile(): string | Uint8Array;
}
