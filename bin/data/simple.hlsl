RWStructuredBuffer<float> src;
RWStructuredBuffer<float> dst;

cbuffer arguments
{
	float bias;
};

[numthreads(64, 1, 1)]
void main(uint3 gID : SV_DispatchThreadID)
{
	float dataOut = bias + sin( src[gID.x] );
	dst[gID.x] = dataOut;
}
