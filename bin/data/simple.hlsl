RWStructuredBuffer<float> src;
RWStructuredBuffer<float> dst;

cbuffer arguments
{
	float bias;
};

[numthreads(64, 1, 1)]
void main(uint3 gID : SV_DispatchThreadID)
{
	dst[gID.x] = bias + sin( src[gID.x] );
}
