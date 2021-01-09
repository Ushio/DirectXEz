#include "pr.hpp"
#include "EzDx.hpp"

void run( ezdx::DeviceObject* deviceObject )
{
	using namespace pr;

	pr::trivial_vector<float> input( 1024 * 1024 * 500 );
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
	ezdx::ConstantBuffer<arguments> constantArg( deviceObject->device() );
	constantArg->bias = 10.0f;

	std::unique_ptr<ezdx::BufferResource> valueBuffer0( new ezdx::BufferResource( deviceObject->device(), ioDataBytes, sizeof( float ), D3D12_RESOURCE_STATE_COPY_DEST ));
	std::unique_ptr<ezdx::BufferResource> valueBuffer1( new ezdx::BufferResource( deviceObject->device(), ioDataBytes, sizeof( float ), D3D12_RESOURCE_STATE_COMMON ) );
	valueBuffer0->setName( L"valueBuffer0" );
	valueBuffer1->setName( L"valueBuffer1" );
	{
		std::unique_ptr<ezdx::UploadResource> uploader(new ezdx::UploadResource(deviceObject->device(), ioDataBytes));
		uploader->setName(L"uploader");
		void* p = uploader->map();
		memcpy(p, input.data(), ioDataBytes);
		uploader->unmap();
		deviceObject->executeCommand([&](ID3D12GraphicsCommandList* commandList) {
			commandList->CopyBufferRegion(
				valueBuffer0->resource(), 0,
				uploader->resource(), 0, valueBuffer0->bytes()
			);
		});
		deviceObject->fence()->wait();
	}
	ezdx::Shader shader( deviceObject->device(), GetDataPath("simple.hlsl").c_str(), GetDataPath("").c_str(), ezdx::CompileMode::Debug );
	std::unique_ptr<ezdx::ArgumentHeap> arg( shader.createDescriptorHeap(deviceObject->device()) );
	arg->RWStructured( "src", valueBuffer0.get());
	arg->RWStructured( "dst", valueBuffer1.get());
	arg->Constant("arguments", &constantArg);

	ezdx::DownloadResource downloadResource( deviceObject->device(), ioDataBytes );

	deviceObject->executeCommand([&]( ID3D12GraphicsCommandList* commandList ) {
		constantArg.updateCommand( commandList );

		// Execute
		shader.dispatch( commandList, arg.get(), ezdx::dispatchsize( numberOfElement, 64 ), 1, 1);

		// download
		ezdx::resourceBarrier( commandList, { valueBuffer1->resourceBarrierTransition( D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE ) } );

		commandList->CopyBufferRegion(
			downloadResource.resource(), 0,
			valueBuffer1->resource(), 0,
			valueBuffer1->bytes()
		);
	});


	std::shared_ptr<ezdx::FenceObject> fence = deviceObject->fence();
	fence->wait();

	ezdx::TypedView<float> results = downloadResource.mapTyped<float>();
	downloadResource.unmap();

	// for debugger tools.
	deviceObject->present();
}

int main()
{
	using namespace pr;
	SetDataDir(JoinPath(ExecutableDir(), "data"));

	// Activate Debug Layer
	ezdx::enableDebugLayer();

	std::vector<ezdx::DxPtr<IDXGIAdapter>> adapters = ezdx::getAllAdapters();

	HRESULT hr;
	std::vector<std::shared_ptr<ezdx::DeviceObject>> devices;
	for ( ezdx::DxPtr<IDXGIAdapter> adapter : adapters )
	{
		DXGI_ADAPTER_DESC d;
		hr = adapter->GetDesc( &d );
		DX_ASSERT( hr == S_OK, "" );

		// The number of bytes of dedicated video memory that are not shared with the CPU.
		if ( d.DedicatedVideoMemory == 0 )
		{
			continue;
		}

		devices.push_back( std::shared_ptr<ezdx::DeviceObject>( new ezdx::DeviceObject( adapter.get() ) ) );
		break;
	}

	for (;;)
	{
		for (auto d : devices)
		{
			printf("run : %s\n", wstring_to_string(d->deviceName()).c_str());
			run( d.get() );
		}
	}
}
