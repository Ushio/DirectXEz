#pragma once

#include <algorithm>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <stdlib.h>
#include <vector>
#include <random>

#include "d3dx12.h"
#include "d3d12shader.h"
#include "dxcapi.h"

#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif

#define DX_ASSERT( status, message )                                                             \
	if ( ( status ) == 0 )                                                                       \
	{                                                                                            \
		char buffer[512];                                                                        \
		snprintf( buffer, sizeof( buffer ), "%s, %s (%d line)\n", message, __FILE__, __LINE__ ); \
		__debugbreak();                                                                          \
	}

namespace ezdx {

template <class T>
class DxPtr
{
public:
	DxPtr() {}
	DxPtr( T* ptr ) : _ptr( ptr )
	{
	}
	DxPtr( const DxPtr<T>& rhs ) : _ptr( rhs._ptr )
	{
		if ( _ptr )
		{
			_ptr->AddRef();
		}
	}
	DxPtr<T>& operator=( const DxPtr<T>& rhs )
	{
		auto p = _ptr;

		if ( rhs._ptr )
		{
			rhs._ptr->AddRef();
		}
		_ptr = rhs._ptr;

		if ( p )
		{
			p->Release();
		}
		return *this;
	}
	~DxPtr()
	{
		if ( _ptr )
		{
			_ptr->Release();
		}
	}
	T* get()
	{
		return _ptr;
	}
	const T* get() const
	{
		return _ptr;
	}
	T* operator->()
	{
		return _ptr;
	}
	T** getAddressOf()
	{
		return &_ptr;
	}
	operator bool()
	{
		return _ptr != nullptr;
	}

private:
	T* _ptr = nullptr;
};

inline void enableDebugLayer()
{
	DxPtr<ID3D12Debug> debugController;
	if ( D3D12GetDebugInterface( IID_PPV_ARGS( debugController.getAddressOf() ) ) != S_OK )
	{
		return;
	}
	debugController->EnableDebugLayer();

	DxPtr<ID3D12Debug3> debug;
	if (debugController->QueryInterface(IID_PPV_ARGS(debug.getAddressOf())) != S_OK)
	{
		return;
	}
	debug->SetEnableGPUBasedValidation(true);
}

inline std::vector<DxPtr<IDXGIAdapter>> getAllAdapters()
{
	HRESULT hr;

	DxPtr<IDXGIFactory7> factory;
	UINT flagsDXGI = DXGI_CREATE_FACTORY_DEBUG;
	hr = CreateDXGIFactory2( flagsDXGI, IID_PPV_ARGS( factory.getAddressOf() ) );
	DX_ASSERT( hr == S_OK, "" );

	std::vector<DxPtr<IDXGIAdapter>> adapters;

	int adapterIndex = 0;
	for ( ;; )
	{
		DxPtr<IDXGIAdapter> adapter;
		hr = factory->EnumAdapters( adapterIndex++, adapter.getAddressOf() );
		if ( hr == S_OK )
		{
			adapters.push_back( adapter );
			continue;
		}
		DX_ASSERT( hr == DXGI_ERROR_NOT_FOUND, "" );
		break;
	};

	return adapters;
}

static void resourceBarrier( ID3D12GraphicsCommandList* commandList, std::vector<D3D12_RESOURCE_BARRIER> barrier )
{
	commandList->ResourceBarrier( barrier.size(), barrier.data() );
}
static HRESULT assertResourceState( ID3D12GraphicsCommandList* commandList, ID3D12Resource* resource, D3D12_RESOURCE_STATES state )
{
	DxPtr<ID3D12DebugCommandList> debugCommandList;

	HRESULT hr;
	hr = commandList->QueryInterface(IID_PPV_ARGS(debugCommandList.getAddressOf()));
	if( S_OK != hr )
	{
		return hr;
	}
	BOOL expected = debugCommandList->AssertResourceState(resource, 0, state);
	DX_ASSERT(expected, "");
	return S_OK;
}

inline int64_t alignedExpand( int64_t x, int64_t align )
{
	return ( ( x + align - 1 ) / align ) * align;
}

class CommandObject
{
public:
	CommandObject( const CommandObject& ) = delete;
	void operator=( const CommandObject& ) = delete;

	CommandObject( ID3D12Device* device )
	{
		HRESULT hr;
		hr = device->CreateCommandAllocator( D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS( _allocator.getAddressOf() ) );
		DX_ASSERT( hr == S_OK, "" );

		hr = device->CreateCommandList(
			0,
			D3D12_COMMAND_LIST_TYPE_DIRECT,
			_allocator.get(),
			nullptr, /* pipeline state */
			IID_PPV_ARGS( _list.getAddressOf() ) );
		DX_ASSERT( hr == S_OK, "" );
	}
	ID3D12GraphicsCommandList* list()
	{
		return _list.get();
	}
	void scopedStoreCommand( std::function<void( ID3D12GraphicsCommandList* commandList )> f )
	{
		if (_isClosed)
		{
			_list->Reset(_allocator.get(), nullptr);
		}

		f( _list.get() );
		_list->Close();
		_isClosed = true;
	}
private:
	bool _isClosed = false;
	DxPtr<ID3D12CommandAllocator> _allocator;
	DxPtr<ID3D12GraphicsCommandList> _list;
};

class DeviceObject
{
public:
	DeviceObject( const DeviceObject& ) = delete;
	void operator=( const DeviceObject& ) = delete;

	DeviceObject( IDXGIAdapter* adapter )
	{
		HRESULT hr;

		DXGI_ADAPTER_DESC d;
		hr = adapter->GetDesc( &d );
		DX_ASSERT( hr == S_OK, "" );

		_deviceName = d.Description;

		struct DeviceIID
		{
			IID iid;
			const char* type;
		};
#define DEVICE_VER( type )      \
	{                           \
		__uuidof( type ), #type \
	}
		const DeviceIID deviceIIDs[] = {
			DEVICE_VER( ID3D12Device8 ),
			DEVICE_VER( ID3D12Device7 ),
			DEVICE_VER( ID3D12Device6 ),
			DEVICE_VER( ID3D12Device5 ),
			DEVICE_VER( ID3D12Device4 ),
			DEVICE_VER( ID3D12Device3 ),
			DEVICE_VER( ID3D12Device2 ),
			DEVICE_VER( ID3D12Device1 ),
		};
#undef DEVICE_VER

		for ( auto deviceIID : deviceIIDs )
		{
			hr = D3D12CreateDevice( adapter, D3D_FEATURE_LEVEL_12_0, deviceIID.iid, (void**)_device.getAddressOf() );
			if ( hr == S_OK )
			{
				_deviceIIDType = deviceIID.type;
				break;
			}
		}
		DX_ASSERT( hr == S_OK, "" );

		D3D12_FEATURE_DATA_SHADER_MODEL shaderModelFeature = {};
		shaderModelFeature.HighestShaderModel = D3D_SHADER_MODEL_6_6;
		hr = _device->CheckFeatureSupport( D3D12_FEATURE_SHADER_MODEL, &shaderModelFeature, sizeof( shaderModelFeature ) );
		DX_ASSERT( hr == S_OK, "" );

		std::map<D3D_SHADER_MODEL, std::string> sm_to_s =
			{
				{ D3D_SHADER_MODEL_5_1, "D3D_SHADER_MODEL_5_1" },
				{ D3D_SHADER_MODEL_6_0, "D3D_SHADER_MODEL_6_0" },
				{ D3D_SHADER_MODEL_6_1, "D3D_SHADER_MODEL_6_1" },
				{ D3D_SHADER_MODEL_6_2, "D3D_SHADER_MODEL_6_2" },
				{ D3D_SHADER_MODEL_6_3, "D3D_SHADER_MODEL_6_3" },
				{ D3D_SHADER_MODEL_6_4, "D3D_SHADER_MODEL_6_4" },
				{ D3D_SHADER_MODEL_6_5, "D3D_SHADER_MODEL_6_5" },
				{ D3D_SHADER_MODEL_6_6, "D3D_SHADER_MODEL_6_6" },
			};
		_highestShaderModel = sm_to_s[shaderModelFeature.HighestShaderModel];

		D3D12_FEATURE_DATA_D3D12_OPTIONS1 option1 = {};
		hr = _device->CheckFeatureSupport( D3D12_FEATURE_D3D12_OPTIONS1, &option1, sizeof( option1 ) );
		DX_ASSERT( hr == S_OK, "" );
		_waveLaneCount = option1.WaveLaneCountMin;
		_totalLaneCount = option1.TotalLaneCount;

		D3D12_COMMAND_QUEUE_DESC commandQueueDesk = {};
		commandQueueDesk.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		commandQueueDesk.Priority = D3D12_COMMAND_QUEUE_PRIORITY_HIGH;
		commandQueueDesk.Flags = D3D12_COMMAND_QUEUE_FLAG_DISABLE_GPU_TIMEOUT;
		commandQueueDesk.NodeMask = 0;
		hr = _device->CreateCommandQueue(&commandQueueDesk, IID_PPV_ARGS(_queue.getAddressOf()));
		DX_ASSERT(hr == S_OK, "");

		_command = std::unique_ptr<CommandObject>( new CommandObject( _device.get() ) );

		DxPtr<IDXGIFactory4> pDxgiFactory;
		hr = CreateDXGIFactory1( __uuidof( IDXGIFactory1 ), (void**)pDxgiFactory.getAddressOf() );
		DX_ASSERT( hr == S_OK, "" );

		DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
		swapChainDesc.BufferCount = 2;
		swapChainDesc.Width = 64;
		swapChainDesc.Height = 64;
		swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		swapChainDesc.SampleDesc.Count = 1;

		// Create the swap chain
		hr = pDxgiFactory->CreateSwapChainForComposition( _queue.get(), &swapChainDesc, nullptr, _swapchain.getAddressOf() );
		DX_ASSERT( hr == S_OK, "" );
	}
	ID3D12Device* device()
	{
		return _device.get();
	}
	void present()
	{
		HRESULT hr;
		hr = _swapchain->Present( 1, 0 );
		DX_ASSERT( hr == S_OK, "" );
	}
	std::wstring deviceName() const
	{
		return _deviceName;
	}
	int waveLaneCount() const
	{
		return _waveLaneCount;
	}
	int totalLaneCount() const
	{
		return _totalLaneCount;
	}
	ID3D12CommandQueue* queue()
	{
		return _queue.get();
	}
	void executeCommand(std::function<void(ID3D12GraphicsCommandList* commandList)> f)
	{
		_command->scopedStoreCommand( f );
		ID3D12CommandList* const command[] = { _command->list() };
		_queue->ExecuteCommandLists( 1, command );
	}
private:
	std::string _deviceIIDType;
	std::wstring _deviceName;
	std::string _highestShaderModel;
	int _waveLaneCount = 0;
	int _totalLaneCount = 0;
	DxPtr<ID3D12Device> _device;
	DxPtr<ID3D12CommandQueue> _queue;
	DxPtr<IDXGISwapChain1> _swapchain;
	std::unique_ptr<CommandObject> _command;
};
class FenceObject
{
public:
	FenceObject(const FenceObject&) = delete;
	void operator=(const FenceObject&) = delete;

	FenceObject(DeviceObject* deviceObject)
	{
		HRESULT hr;
		hr = deviceObject->device()->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(_fence.getAddressOf()));
		DX_ASSERT(hr == S_OK, "");
		hr = deviceObject->queue()->Signal(_fence.get(), 1);
		DX_ASSERT(hr == S_OK, "");
	}
	void wait()
	{
		HANDLE e = CreateEvent(nullptr, false, false, nullptr);
		_fence->SetEventOnCompletion(1, e);
		WaitForSingleObject(e, INFINITE);
		CloseHandle(e);
	}
private:
	DxPtr<ID3D12Fence> _fence;
};


class UploadResource
{
public:
	UploadResource( const UploadResource& ) = delete;
	void operator=( const UploadResource& ) = delete;

	UploadResource( ID3D12Device* device, int64_t bytes ) : _bytes( std::max( bytes, 1LL ) )
	{
		HRESULT hr;
		hr = device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_UPLOAD ),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer( _bytes ),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS( _resource.getAddressOf() ) );
		DX_ASSERT( hr == S_OK, "" );
	}

	void* map()
	{
		D3D12_RANGE readrange = {};
		void* p;
		HRESULT hr;
		hr = _resource->Map(0, &readrange, &p);
		DX_ASSERT(hr == S_OK, "");
		return (void*)p;
	}
	void unmap( int64_t writeBytesBeg, int64_t writeBytesEnd )
	{
		D3D12_RANGE writerange = { writeBytesBeg, writeBytesEnd };
		_resource->Unmap( 0, &writerange );
	}
	void unmap()
	{
		unmap( 0, bytes() );
	}
	int64_t bytes()
	{
		return _bytes;
	}
	ID3D12Resource* resource()
	{
		return _resource.get();
	}
	void setName( std::wstring name )
	{
		_resource->SetName( name.c_str() );
	}
private:
	int64_t _bytes;
	DxPtr<ID3D12Resource> _resource;
};

template <class T>
class TypedView
{
public:
	TypedView( void* p, int64_t bytes )
	{
		_p = (T*)p;
		_count = bytes / sizeof(T);
	}

	int64_t count() const 
	{
		return _count;
	}
	T* data()
	{
		return _p;
	}
	const T* data() const
	{
		return _p;
	}
	T& operator[](int64_t i)
	{
		return _p[i];
	}
	const T& operator[](int64_t i) const
	{
		return _p[i];
	}
private:
	T* _p;
	int64_t _count;
};

class BufferResource
{
public:
	BufferResource( const BufferResource& ) = delete;
	void operator=( const BufferResource& ) = delete;

	BufferResource( DeviceObject* deviceObject, int64_t bytes, int64_t structureByteStride, D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON )
		: _bytes( std::max( bytes, 1LL ) ), _structureByteStride( structureByteStride )
	{
		HRESULT hr;
		hr = deviceObject->device()->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_DEFAULT ),
			D3D12_HEAP_FLAG_NONE /* I don't know */,
			&CD3DX12_RESOURCE_DESC::Buffer( _bytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS ),
			initialState,
			nullptr,
			IID_PPV_ARGS( _resource.getAddressOf() ) );
		DX_ASSERT( hr == S_OK, "" );
	}
	int64_t bytes() const
	{
		return _bytes;
	}
	int64_t itemCount() const
	{
		return _bytes / _structureByteStride;
	}
	ID3D12Resource* resource()
	{
		return _resource.get();
	}
	D3D12_UNORDERED_ACCESS_VIEW_DESC UAVDescription() const
	{
		D3D12_UNORDERED_ACCESS_VIEW_DESC d = {};
		d.Format = DXGI_FORMAT_UNKNOWN;
		d.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
		d.Buffer.FirstElement = 0;
		d.Buffer.NumElements = _bytes / _structureByteStride;
		d.Buffer.StructureByteStride = _structureByteStride;
		d.Buffer.CounterOffsetInBytes = 0;
		return d;
	}
	void setName( std::wstring name )
	{
		_resource->SetName( name.c_str() );
	}

	void* mapForWriting( DeviceObject *deviceObject )
	{
		DX_ASSERT( !_uploader, "");

		HRESULT hr;
		hr = deviceObject->device()->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(_bytes),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(_uploader.getAddressOf()));
		DX_ASSERT( hr == S_OK, "" );

		// no read
		D3D12_RANGE range = {};
		void* p;
		hr = _uploader->Map( 0, &range, &p );
		DX_ASSERT(hr == S_OK, "");
		return (void*)p;
	}
	void unmapForWriting( DeviceObject *deviceObject, int64_t bytesBeg, int64_t bytesEnd )
	{
		DX_ASSERT( _uploader, "");
		DX_ASSERT( 0 <= bytesBeg, "");
		DX_ASSERT( bytesEnd <= _bytes, "");

		D3D12_RANGE range = { bytesBeg, bytesEnd };
		_uploader->Unmap( 0, &range );

		deviceObject->executeCommand(
			[&](ID3D12GraphicsCommandList* commandList) {
				commandList->CopyBufferRegion(
					_resource.get(), bytesBeg,
					_uploader.get(), bytesBeg, bytesEnd - bytesBeg
				);
			}
		);

		// wait for copying in order to free upload resource.
		FenceObject fence( deviceObject );
		fence.wait();

		_uploader = DxPtr<ID3D12Resource>();
	}
	template <class T>
	TypedView<T> mapTypedForWriting( DeviceObject* deviceObject )
	{
		void* p = mapForWriting( deviceObject );
		return TypedView<T>(p, bytes());
	}

	void* mapForReading( DeviceObject* deviceObject, int64_t bytesBeg, int64_t bytesEnd )
	{
		DX_ASSERT( !_downloader, "" );
		DX_ASSERT( 0 <= bytesBeg, "" );
		DX_ASSERT( bytesEnd <= _bytes, "" );

		HRESULT hr;
		hr = deviceObject->device()->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(_bytes),
			D3D12_RESOURCE_STATE_COPY_DEST,
			nullptr,
			IID_PPV_ARGS(_downloader.getAddressOf()));
		DX_ASSERT( hr == S_OK, "" );

		deviceObject->executeCommand( 
			[&](ID3D12GraphicsCommandList* commandList) {
				commandList->CopyBufferRegion(
					_downloader.get(), bytesBeg,
					_resource.get(), bytesBeg, bytesEnd - bytesBeg
				);
			}
		);

		// wait for copying
		FenceObject fence(deviceObject);
		fence.wait();

		D3D12_RANGE range = { bytesBeg, bytesEnd };
		void* p;
		hr = _downloader->Map( 0, &range, &p );
		DX_ASSERT(hr == S_OK, "");

		return (void*)p;
	}
	template <class T>
	TypedView<T> mapTypedForReading(DeviceObject* deviceObject, int64_t bytesBeg, int64_t bytesEnd)
	{
		void* p = mapForReading( deviceObject, bytesBeg, bytesEnd );
		return TypedView<T>( p , bytes() );
	}
	void unmapForReading()
	{
		DX_ASSERT( _downloader, "");
		D3D12_RANGE range = {  };
		_downloader->Unmap(0, &range);
		_downloader = DxPtr<ID3D12Resource>();
	}
private:
	int64_t _bytes;
	int64_t _structureByteStride;
	DxPtr<ID3D12Resource> _resource;
	DxPtr<ID3D12Resource> _uploader;
	DxPtr<ID3D12Resource> _downloader;
};

/*
 data is D3D12_HEAP_TYPE_UPLOAD pointer. please make sure this object is alive during shader execution.
*/
template <class T>
class ConstantBuffer
{
public:
	ConstantBuffer( const ConstantBuffer& ) = delete;
	void operator=( const ConstantBuffer& ) = delete;

	ConstantBuffer( DeviceObject* deviceObject )
		: _bytes( alignedExpand( sizeof(T), 256 ) )
	{
		static_assert( 1 <= sizeof(T), "T shouldn't be empty" );

		HRESULT hr;
		hr = deviceObject->device()->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_UPLOAD ),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer( _bytes ),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(_resource.getAddressOf()));
		DX_ASSERT(hr == S_OK, "");

		D3D12_RANGE range = {};
		void* p;
		hr = _resource->Map(0, &range, &p);
		DX_ASSERT(hr == S_OK, "");
		_ptr = (T *)p;
	}
	~ConstantBuffer()
	{
		D3D12_RANGE range = {};
		_resource->Unmap( 0, &range );
	}
	ID3D12Resource* resource()
	{
		return _resource.get();
	}
	int64_t bytes() const {
		return _bytes;
	}
	// please not read from this ptr
	// https://docs.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12resource-map
	T* operator->()
	{
		return _ptr;
	}
private:
	T* _ptr;
	int64_t _bytes = 0;
	DxPtr<ID3D12Resource> _resource;
};

class DXCFileBlob : public IDxcBlob
{
public:
	DXCFileBlob( const char* file ):_counter(1)
	{
		FILE* fp = fopen( file, "rb" );
		if( fp == 0 )
		{
			return;
		}
		fseek( fp, 0, SEEK_END );

		_data.resize( ftell( fp ) );

		fseek( fp, 0, SEEK_SET );

		size_t s = fread( _data.data(), 1, _data.size(), fp );
		DX_ASSERT( s == _data.size(), "failed to load file." );

		fclose( fp );
		fp = nullptr;
	}
	LPVOID STDMETHODCALLTYPE GetBufferPointer(void) override 
	{
		return _data.data();
	}
	SIZE_T STDMETHODCALLTYPE GetBufferSize(void) override
	{
		return _data.size();
	}
	virtual ULONG STDMETHODCALLTYPE AddRef(void)
	{
		ULONG c = _counter++;
		return c + 1;
	}
	virtual ULONG STDMETHODCALLTYPE Release(void)
	{
		ULONG c = _counter--;
		if (c - 1 == 0)
		{
			delete this;
		}
		return c - 1;
	}

	HRESULT STDMETHODCALLTYPE QueryInterface(
		/* [in] */ REFIID riid,
		/* [iid_is][out] */ _COM_Outptr_ void __RPC_FAR* __RPC_FAR* ppvObject)
	{
		if ((riid == __uuidof(IDxcBlob)) || (riid == IID_IUnknown))
		{
			*ppvObject = (void*)this;
			AddRef(); // -- Maintain the reference count
		}
		else
			*ppvObject = NULL;

		return (*ppvObject == NULL) ? E_NOINTERFACE : S_OK;
	}
private:
	std::atomic<ULONG> _counter;
	std::vector<uint8_t> _data;
};


class Compiler
{
public:
	Compiler()
	{
		HRESULT hr;
		hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(_dxUtils.getAddressOf()));
		DX_ASSERT(hr == S_OK, "");
		hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(_dxCompiler.getAddressOf()));
		DX_ASSERT(hr == S_OK, "");
	}
	static Compiler& compiler()
	{
		static Compiler c;
		return c;
	}
	IDxcUtils* dxUtils()
	{
		return _dxUtils.get();
	}
	IDxcCompiler3* dxCompiler()
	{
		return _dxCompiler.get();
	}
private:
	DxPtr<IDxcUtils> _dxUtils;
	DxPtr<IDxcCompiler3> _dxCompiler;
};
enum class CompileMode
{
	Release,
	Debug
};

class ArgumentHeap
{
public:
	ArgumentHeap( ID3D12Device* device, std::map<std::string, int> var2index) :_var2index(var2index) {
		HRESULT hr;
		D3D12_DESCRIPTOR_HEAP_DESC desc = {};
		desc.NumDescriptors = _var2index.size();
		desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		hr = device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(_bufferHeap.getAddressOf()));
		DX_ASSERT(hr == S_OK, "");

		_increment = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	
		device->AddRef();
		_device = DxPtr<ID3D12Device>(device);
	}
	void RWStructured( const char *var, BufferResource *resource )
	{
		DX_ASSERT(_var2index.count(var), "");
		D3D12_UNORDERED_ACCESS_VIEW_DESC d = resource->UAVDescription();
		D3D12_CPU_DESCRIPTOR_HANDLE h = _bufferHeap->GetCPUDescriptorHandleForHeapStart();
		h.ptr += _increment * _var2index[var];
		_device->CreateUnorderedAccessView( resource->resource(), nullptr, &d, h );
	}
	template <class T>
	void Constant( const char* var, ConstantBuffer<T>* resource )
	{
		DX_ASSERT(_var2index.count(var), "");
		D3D12_CONSTANT_BUFFER_VIEW_DESC d = {};
		d.BufferLocation = resource->resource()->GetGPUVirtualAddress();
		d.SizeInBytes = resource->bytes();
		D3D12_CPU_DESCRIPTOR_HANDLE h = _bufferHeap->GetCPUDescriptorHandleForHeapStart();
		h.ptr += _increment * _var2index[var];
		_device->CreateConstantBufferView(&d, h);
	}
	template <class T>
	void ConstantGlobal(ConstantBuffer<T>* resource)
	{
		Constant("$Globals", resource);
	}
	ID3D12DescriptorHeap* descriptorHeap()
	{
		return _bufferHeap.get();
	}
private:
	uint32_t _increment;
	std::map<std::string, int> _var2index;
	DxPtr<ID3D12DescriptorHeap> _bufferHeap;
	DxPtr<ID3D12Device> _device;
};

class Shader
{
public:
	Shader( DeviceObject *deviceObject, const char *filename, const char *includeDir, CompileMode compileMode )
	{
		HRESULT hr;
		DxPtr<IDxcIncludeHandler> pIncludeHandler;
		hr = Compiler::compiler().dxUtils()->CreateDefaultIncludeHandler(pIncludeHandler.getAddressOf());
		DX_ASSERT(hr == S_OK, "");

		std::wstring I = pr::string_to_wstring(std::string(includeDir));
		std::vector<const wchar_t*> args = {
			L"simple.hlsl",

			L"-T", L"cs_6_5",
			L"-I", I.c_str(),
		};
		if( compileMode == CompileMode::Debug )
		{
			args.push_back(L"-Zi"); // Enable debug information
			args.push_back(L"-Od"); // Disable optimizations
			args.push_back(L"-Qembed_debug"); // Embed PDB in shader container (must be used with /Zi)
		}

		DxPtr<IDxcBlob> shaderFile( new DXCFileBlob( filename ) );
		DX_ASSERT(shaderFile->GetBufferSize() != 0, "");

		DxcBuffer buffer = { };
		buffer.Ptr = shaderFile->GetBufferPointer();
		buffer.Size = shaderFile->GetBufferSize();
		buffer.Encoding = DXC_CP_ACP;
		
		std::string ilFile;
		{
			std::vector<const wchar_t*> args_preprocess = args;
			args_preprocess.push_back(L"-P");
			args_preprocess.push_back(L"preprocessed.hlsl");
			DxPtr<IDxcResult> compileResult;
			hr = Compiler::compiler().dxCompiler()->Compile(
				&buffer,
				args_preprocess.data(),
				args_preprocess.size(),
				pIncludeHandler.get(),
				IID_PPV_ARGS(compileResult.getAddressOf()) // Compiler output status, buffer, and errors.
			);
			DX_ASSERT(hr == S_OK, "");

			DxPtr<IDxcBlobUtf8> hlsl;
			DxPtr<IDxcBlobUtf16> name;
			hr = compileResult->GetOutput(DXC_OUT_HLSL, IID_PPV_ARGS(hlsl.getAddressOf()), name.getAddressOf());

			if (hlsl && hlsl->GetBufferSize())
			{
				char shaderHash[9] = {};
				uint32_t h = pr::xxhash32(hlsl->GetBufferPointer(), hlsl->GetBufferSize(), 0);
				sprintf(shaderHash, "%08x", h);

				std::string ilname = pr::GetPathBasenameWithoutExtension(filename) + "_" + shaderHash + ".il";
				if (compileMode == CompileMode::Debug)
				{
					ilname += "_d";
				}
				ilFile = pr::JoinPath(pr::GetPathDirname(filename), ilname);
			}
		}

		DxPtr<IDxcBlob> ilBlob;
		DxPtr<IDxcBlob> ilBlobFromFile(new DXCFileBlob(ilFile.c_str()));
		if( ilBlobFromFile->GetBufferSize() )
		{
			ilBlob = ilBlobFromFile;
		}
		else
		{
			DxPtr<IDxcResult> compileResult;
			hr = Compiler::compiler().dxCompiler()->Compile(
				&buffer,
				args.data(),
				args.size(),
				pIncludeHandler.get(),
				IID_PPV_ARGS(compileResult.getAddressOf()) // Compiler output status, buffer, and errors.
			);
			DX_ASSERT(hr == S_OK, "");

			DxPtr<IDxcBlobUtf8> compileErrors;
			hr = compileResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(compileErrors.getAddressOf()), nullptr);
			DX_ASSERT(hr == S_OK, "");

			hr = compileResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(ilBlob.getAddressOf()), nullptr);
			DX_ASSERT(hr == S_OK, "");

			if( compileErrors.get() && compileErrors->GetStringLength() != 0 )
			{
				printf("Warnings and Errors:\n%s\n", compileErrors->GetStringPointer());
			}

			if( ilBlob && 0 < ilBlob->GetBufferSize() )
			{
				static std::random_device rd;
				static std::mt19937 e { rd() };
				static std::mutex m;
				std::lock_guard<std::mutex> lock(m);
				std::uniform_int_distribution<int> gen { 0, 23 };
				char tmp[9] = {};
				for (int i = 0; i < 8; ++i)
				{
					tmp[i] = 'a' + gen( e );
				}
				std::string tmpName = pr::GetPathBasenameWithoutExtension(filename) + "_" + tmp;
				std::string tmpFile = pr::JoinPath(pr::GetPathDirname(filename), tmpName);

				FILE* fp = fopen(tmpFile.c_str(), "wb");
				fwrite(ilBlob->GetBufferPointer(), ilBlob->GetBufferSize(), 1, fp);
				fclose(fp);
				
				if( rename(tmpFile.c_str(), ilFile.c_str()) != 0 )
				{
					remove(tmpFile.c_str());
				}
			}
			else
			{
				DX_ASSERT(0, "");
			}
		}
		
		DxPtr<IDxcContainerReflection> reflectionContainer;
		UINT32 shaderIdx;
		hr = DxcCreateInstance(CLSID_DxcContainerReflection, IID_PPV_ARGS(reflectionContainer.getAddressOf()) );
		DX_ASSERT(hr == S_OK, "");
		hr = reflectionContainer->Load(ilBlob.get());
		DX_ASSERT(hr == S_OK, "");
		hr = reflectionContainer->FindFirstPartKind(MAKEFOURCC('D', 'X', 'I', 'L'), &shaderIdx);
		DX_ASSERT(hr == S_OK, "");

		DxPtr<ID3D12ShaderReflection> reflection;
		hr = reflectionContainer->GetPartReflection(shaderIdx, IID_PPV_ARGS(reflection.getAddressOf()));
		DX_ASSERT(hr == S_OK, "");

		// Use reflection interface here.
		D3D12_SHADER_DESC desc = {};
		reflection->GetDesc(&desc);

		std::vector<D3D12_DESCRIPTOR_RANGE> bufferDescriptorRanges;
		for (auto i = 0; i < desc.BoundResources; ++i)
		{
			D3D12_SHADER_INPUT_BIND_DESC bind = {};
			reflection->GetResourceBindingDesc(i, &bind);
			D3D12_DESCRIPTOR_RANGE range = {};
			switch (bind.Type)
			{
			case D3D_SIT_CBUFFER:
				range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
				break;
			case D3D_SIT_STRUCTURED:
				range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
				break;
			case D3D_SIT_UAV_RWTYPED:
			case D3D_SIT_UAV_RWSTRUCTURED:
				range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
				break;
			default:
				DX_ASSERT(0, "");
			}

			range.NumDescriptors = 1;
			range.BaseShaderRegister = bind.BindPoint;
			range.RegisterSpace = bind.Space;
			range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
			bufferDescriptorRanges.push_back(range);
			_var2index[bind.Name] = i;
		}

		D3D12_ROOT_PARAMETER rootParameter = {};
		rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		rootParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		rootParameter.DescriptorTable.NumDescriptorRanges = bufferDescriptorRanges.size();
		rootParameter.DescriptorTable.pDescriptorRanges = bufferDescriptorRanges.data();

		// Signature
		D3D12_ROOT_SIGNATURE_DESC rsDesc = CD3DX12_ROOT_SIGNATURE_DESC(1, &rootParameter);
		DxPtr<ID3DBlob> signatureBlob;
		hr = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, signatureBlob.getAddressOf(), nullptr);
		DX_ASSERT(hr == S_OK, "");

		hr = deviceObject->device()->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(_signature.getAddressOf()));
		DX_ASSERT(hr == S_OK, "");

		D3D12_COMPUTE_PIPELINE_STATE_DESC ppDesc = {};
		ppDesc.CS.pShaderBytecode = ilBlob->GetBufferPointer();
		ppDesc.CS.BytecodeLength = ilBlob->GetBufferSize();
		ppDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
		ppDesc.NodeMask = 0;
		ppDesc.pRootSignature = _signature.get();
		hr = deviceObject->device()->CreateComputePipelineState(&ppDesc, IID_PPV_ARGS(_csPipeline.getAddressOf()));
		DX_ASSERT(hr == S_OK, "");
	}
	ArgumentHeap* createArgumentHeap( ID3D12Device* device ) const
	{
		return new ArgumentHeap( device, _var2index );
	}

	// asynchronous
	void dispatch( DeviceObject* deviceObject, ArgumentHeap* arg, int64_t x, int64_t y, int64_t z)
	{
		deviceObject->executeCommand([&](ID3D12GraphicsCommandList* commandList) {
			ID3D12DescriptorHeap* heap = arg->descriptorHeap();
			commandList->SetDescriptorHeaps( 1, &heap );
			commandList->SetPipelineState(_csPipeline.get());
			commandList->SetComputeRootSignature(_signature.get());
			commandList->SetComputeRootDescriptorTable( 0, heap->GetGPUDescriptorHandleForHeapStart() );
			commandList->Dispatch( x, y, z );
		});
	}
private:
	DxPtr<ID3D12RootSignature> _signature;
	DxPtr<ID3D12PipelineState> _csPipeline;
	std::map<std::string, int> _var2index;
};

} // ezdx