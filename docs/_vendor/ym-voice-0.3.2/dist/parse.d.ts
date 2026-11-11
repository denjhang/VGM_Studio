import { OPMVoice } from "./opm-voice";
export declare function parseOPM(fileData: string): {
    voices: {
        [voiceNumber: number]: OPMVoice;
    };
    comments: {
        [voiceNumber: number]: string[];
    };
};
