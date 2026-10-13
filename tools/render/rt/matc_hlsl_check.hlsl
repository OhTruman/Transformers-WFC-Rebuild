// compile check: every material of a map behind the uber-switch, inputs from a buffer
#include "materials_rt.hlsl"
StructuredBuffer<MatIn> Inputs : register(t0);
StructuredBuffer<uint> Ids : register(t1);
RWStructuredBuffer<MatOut> Outputs : register(u0);
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    MatIn m = Inputs[id.x];
    MatOut o;
    wfcEvalMaterial(Ids[id.x], m, o);
    Outputs[id.x] = o;
}
