export declare abstract class YMVoice {
    __type: string;
    abstract encode(): Array<number>;
    constructor(__type: string);
    toHash(): string;
    abstract toMML(type?: string | null): string;
    abstract toFile(type?: string | null, parameters?: {
        [key: string]: unknown;
    }): string | Uint8Array;
}
