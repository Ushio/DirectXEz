#include "pr.hpp"
#include "EzDx.hpp"

void run( ezdx::DeviceObject* deviceObject )
{
	using namespace pr;

	uint64_t numberOfElement = 1024 * 1024 * 128;
	uint64_t ioDataBytes = sizeof( float ) * numberOfElement;

	struct arguments
	{
		float bias;
	};
	ezdx::ConstantBuffer<arguments> constantArg( deviceObject );
	constantArg->bias = 10.0f;

	std::unique_ptr<ezdx::BufferResource> valueBuffer0( new ezdx::BufferResource( deviceObject, ioDataBytes, sizeof( float ) ) );
	std::unique_ptr<ezdx::BufferResource> valueBuffer1( new ezdx::BufferResource( deviceObject, ioDataBytes, sizeof( float ) ) );
	valueBuffer0->setName( L"valueBuffer0" );
	valueBuffer1->setName( L"valueBuffer1" );
	ezdx::TypedView<float> value0View = valueBuffer0->mapTypedForWriting<float>( deviceObject );
	for (int i = 0; i < value0View.count(); ++i)
	{
		value0View[i] = i / 10.0f;
	}
	valueBuffer0->unmapForWriting( deviceObject, 0, ioDataBytes );

	ezdx::Shader shader( deviceObject, GetDataPath("simple.hlsl").c_str(), GetDataPath("").c_str(), ezdx::CompileMode::Debug );
	std::unique_ptr<ezdx::ArgumentHeap> arg( shader.createArgumentHeap(deviceObject->device()) );
	arg->RWStructured( "src", valueBuffer0.get());
	arg->RWStructured( "dst", valueBuffer1.get());
	arg->Constant("arguments", &constantArg);

	for (int i = 0; i < 3; ++i)
	{
		shader.dispatch( deviceObject, arg.get(), ezdx::alignedExpand(numberOfElement, 64) / 64, 1, 1);
	}

	ezdx::TypedView<float> value1View = valueBuffer1->mapTypedForReading<float>(deviceObject, 0, valueBuffer1->bytes());
	//for (int i = 0; i < value1View.count(); ++i)
	//{
	//	float truth = 10.0f + std::sin(i / 10.0f);
	//	float d = std::fabs( value1View[i] - truth);
	//	printf("%f, %f\n", value1View[i], d );
	//}
	valueBuffer1->unmapForReading();

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
