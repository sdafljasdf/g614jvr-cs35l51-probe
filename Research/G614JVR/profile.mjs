// Research-only G614JVR board identity. This module is NOT linked to the kext.
// It implements neither IORegistry binding, MMIO, GPIO nor SPI, and cannot enable audio.
export const ALLOW_REAL_HARDWARE_ACCESS = false;
export const G614JVR_EXPERIMENTAL_ENABLE = false;
export const profile = Object.freeze({subsystem:'1043:1B93',acpiHid:'CSC3551',bus:'SPI',acpiController:'\\_SB.PC00.SPI3',endpoints:Object.freeze([{chipSelect:0,physicalChannel:null},{chipSelect:1,physicalChannel:null}].map(Object.freeze)),reportedTuning:'V0/JGX',reportedTuningSource:'user supplied Windows evidence, not independently reviewed',silicon:'CS35L51',siliconConfidence:'HIGH_OEM_PLATFORM',siliconIndividuallyProbed:false,revision:null,boost:null,calibration:null});
export class CS35L4xFamilyScaffold {
  constructor(){this.kind='CS35L51';}
  identify(){return {verdict:'UNSUPPORTED_AMPLIFIER',reason:'NO_VALIDATED_CS35L51_DRIVER'};}
  initialize(){throw Error('FAIL_CLOSED: no hardware implementation');}
}
export class MockCS35L4xTransport {
  constructor(){this.events=[];}
  inspect(){this.events.push('mock-only');return {verdict:'NOT_READY',reason:'real transport unavailable'};}
}
export function attach(p,transport){
 if(ALLOW_REAL_HARDWARE_ACCESS!==false||G614JVR_EXPERIMENTAL_ENABLE!==false)throw Error('gate violated');
 if(p!==profile||Object.getPrototypeOf(transport)!==MockCS35L4xTransport.prototype)throw Error('unknown profile or transport');
 return {verdict:'NOT_READY',board:p.subsystem,bus:p.bus,endpoints:p.endpoints,reason:'NO_REAL_PROBE'};
}
