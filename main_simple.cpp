#include "pr.hpp"
#include "EzDx.hpp"

void run( DeviceObject* deviceObject )
{
	using namespace pr;

	pr::trivial_vector<float> input( 1024 * 1024 );
	for ( int i = 0; i < input.size(); ++i )
	{
		input[i] = i / 10.0f;
	}
	uint64_t numberOfElement = input.size();
	uint64_t ioDataBytes = sizeof( float ) * input.size();

	struct arguments
	{
		float bias;
	};
	ConstantBuffer<arguments> constantArg( deviceObject->device() );
	constantArg->bias = 10.0f;

	std::unique_ptr<BufferResource> valueBuffer0( new BufferResource( deviceObject->device(), ioDataBytes, sizeof( float ), D3D12_RESOURCE_STATE_COPY_DEST ));
	std::unique_ptr<BufferResource> valueBuffer1( new BufferResource( deviceObject->device(), ioDataBytes, sizeof( float ), D3D12_RESOURCE_STATE_COMMON ) );
	valueBuffer0->setName( L"valueBuffer0" );
	valueBuffer1->setName( L"valueBuffer1" );
	{
		std::unique_ptr<UploadResource> uploader(new UploadResource(deviceObject->device(), ioDataBytes));
		uploader->setName(L"uploader");
		void* p = uploader->map();
		memcpy(p, input.data(), ioDataBytes);
		uploader->unmap(0, uploader->bytes());
		deviceObject->executeCommand([&](ID3D12GraphicsCommandList* commandList) {
			commandList->CopyBufferRegion(
				valueBuffer0->resource(), 0,
				uploader->resource(), 0, valueBuffer0->bytes()
			);
			resourceBarrier(commandList, { valueBuffer0->resourceBarrierTransition( D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON ) });
		});
		deviceObject->fence()->wait();
	}
	Shader shader( deviceObject->device(), GetDataPath("simple.hlsl").c_str(), GetDataPath("").c_str(), CompileMode::Debug );
	std::unique_ptr<ArgumentHeap> arg( shader.createDescriptorHeap(deviceObject->device()) );
	arg->RWStructured( "src", valueBuffer0.get());
	arg->RWStructured( "dst", valueBuffer1.get());
	arg->Constant("arguments", &constantArg);

	DownloadResource downloadResource( deviceObject->device(), ioDataBytes );

	deviceObject->executeCommand([&]( ID3D12GraphicsCommandList* commandList ) {
		constantArg.updateCommand( commandList );

		// Execute
		shader.dispatch( commandList, arg.get(), dispatchsize( numberOfElement, 64 ), 1, 1);

		// download
		resourceBarrier( commandList, { valueBuffer1->resourceBarrierTransition( D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE ) } );

		commandList->CopyBufferRegion(
			downloadResource.resource(), 0,
			valueBuffer1->resource(), 0,
			valueBuffer1->bytes()
		);

		resourceBarrier(commandList, { valueBuffer1->resourceBarrierTransition(D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON) });
	});


	std::shared_ptr<FenceObject> fence = deviceObject->fence();
	fence->wait();

	memcpy(
		input.data(),
		downloadResource.map(0, downloadResource.bytes()),
		downloadResource.bytes()
	);
	downloadResource.unmap();

	// for debugger tools.
	deviceObject->present();
}

int main()
{
	using namespace pr;
	SetDataDir(JoinPath(ExecutableDir(), "data"));

	// Activate Debug Layer
	enableDebugLayer();

	std::vector<DxPtr<IDXGIAdapter>> adapters = getAllAdapters();

	HRESULT hr;
	std::vector<std::shared_ptr<DeviceObject>> devices;
	for ( DxPtr<IDXGIAdapter> adapter : adapters )
	{
		DXGI_ADAPTER_DESC d;
		hr = adapter->GetDesc( &d );
		DX_ASSERT( hr == S_OK, "" );

		// The number of bytes of dedicated video memory that are not shared with the CPU.
		if ( d.DedicatedVideoMemory == 0 )
		{
			continue;
		}

		devices.push_back( std::shared_ptr<DeviceObject>( new DeviceObject( adapter.get() ) ) );
	}

	for (auto d : devices)
	{
		printf("run : %s\n", wstring_to_string(d->deviceName()).c_str());
		run( d.get() );
	}
}
