// Matthäus G. Chajdas
// Licensed under the 3-clause BSD license

#include "clInfo.h"

#include <iostream>
#include <iomanip>
#include <vector>
#include <iterator>

#include <list>
#include <vector>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include <cassert>
#include <algorithm>

#if _MSC_VER
#pragma warning (disable: 4127)
#endif

#ifdef __APPLE__
	#include <OpenCL/cl.h>
#else
	#include <CL/cl.h>
#endif

#define NIV_SAFE_CL(expr) do{const auto r = (expr); if (r != CL_SUCCESS) { std::cerr << #expr << " failed with error code " << r << "\n"; return nullptr; }}while(0)

// Thanks, gnu_dev_major
#ifdef major
#undef major
#endif

#ifdef minor
#undef minor
#endif

namespace {
/**
Simple memory pool.

Allocates memory in blocks. Assumes all entries are POD types.
*/
template <int BlockSize = 1048576>
struct Pool
{
public:
	void* Allocate (int size)
	{
		if (size < 0 || size >= BlockSize) {
			throw std::bad_alloc ();
		}

		if ((currentBlockOffset_ + size) > BlockSize) {
			blocks_.emplace_back (std::vector<unsigned char> (BlockSize));

			currentBlockOffset_ = 0;
			currentBlock_ = &(blocks_.back ());
		}

		// Align everything to 8 byte
		size = ((size + 7) / 8) * 8;
		auto result = currentBlock_->data () + currentBlockOffset_;
		currentBlockOffset_ += size;

		return result;
	}

	template <typename T>
	T* Allocate ()
	{
		return static_cast<T*> (this->Allocate (sizeof (T)));
	}

private:
	std::list<std::vector<unsigned char>>	blocks_;
	std::vector<unsigned char>*				currentBlock_ = nullptr;
	int currentBlockOffset_ = BlockSize;
};

struct Version
{
	int major = 0;
	int minor = 0;

	Version () = default;

	Version (const int major, const int minor)
	: major (major)
	, minor (minor)
	{
	}

	Version (const int major)
	: Version (major, 0)
	{
	}

	bool operator== (const Version& other) const
	{
		return major == other.major && minor == other.minor;
	}

	bool operator!= (const Version& other) const
	{
		return major != other.major || minor != other.minor;
	}

	bool operator< (const Version& other) const
	{
		if (major != other.major) {
			return major < other.major;
		}

		return minor < other.minor;
	}

	bool operator<= (const Version& other) const
	{
		if (major != other.major) {
			return major < other.major;
		}

		return minor <= other.minor;
	}

	bool operator>= (const Version& other) const
	{
		if (major != other.major) {
			return major > other.major;
		}

		return minor >= other.minor;
	}

	bool operator> (const Version& other) const
	{
		if (major != other.major) {
			return major > other.major;
		}

		return minor > other.minor;
	}
};

////////////////////////////////////////////////////////////////////////////////
Version ParseVersion (const char* s)
{
	// Version string format is: OpenCL 1.0 VENDOR_SPECIFIC_STUFF
	//								   ^   ^ guaranteed spaces
	// We search for first space, then run until second space, and
	// copy to minorVersion/majorVersion
	// Version number should be always less than 16 bytes
	char minorVersion [16] = { 0 };
	char majorVersion [16] = { 0 };

	// Skip OpenCL<SPACE>
	while (*s != ' ') {
		++s;
	}

	++s;

	// Major version
	const char* majorStart = s;
	while (*s != '.') {
		++s;
	}

	assert ((s-majorStart) < 16);
	::memcpy (majorVersion, majorStart, s - majorStart);
	++s;

	// Minor
	const char* minorStart = s;
	while (*s != ' ') {
		++s;
	}

	assert ((s-minorStart) < 16);
	::memcpy (minorVersion, minorStart, s - minorStart);

	return { std::atoi (majorVersion), std::atoi (minorVersion) };
}

////////////////////////////////////////////////////////////////////////////////
const char* ChannelOrderToString (cl_channel_order order)
{
	switch (order) {
	case CL_R: return "R";
	case CL_A: return "A";
	case CL_RG: return "RG";
	case CL_RA: return "RA";
	case CL_RGB: return "RGB";
	case CL_RGBA: return "RGBA";
	case CL_BGRA: return "BGRA";
	case CL_ARGB: return "ARGB";
	case CL_INTENSITY: return "INTENSITY";
	case CL_LUMINANCE: return "LUMINANCE";
	case CL_Rx: return "Rx";
	case CL_RGx: return "RGx";
	case CL_RGBx: return "RGBx";
#if CL_VERSION_2_0
	case CL_DEPTH_STENCIL: return "DEPTH_STENCIL";
#endif

	default: return "Unknown channel order";
	}
}

////////////////////////////////////////////////////////////////////////////////
const char* ChannelDataTypeToString (cl_channel_type type)
{
	switch (type) {
	case CL_SNORM_INT8: return "int8_snorm";
	case CL_SNORM_INT16: return "int16_snorm";
	case CL_UNORM_INT8: return "int8_unorm";
	case CL_UNORM_INT16: return "int16_unorm";
	case CL_UNORM_SHORT_565: return "short565_unorm";
	case CL_UNORM_SHORT_555: return "short555_unorm";
	case CL_UNORM_INT_101010: return "int101010_unorm";
	case CL_SIGNED_INT8: return "sint8";
	case CL_SIGNED_INT16: return "sint16";
	case CL_SIGNED_INT32: return "sint32";
	case CL_UNSIGNED_INT8: return "uint8";
	case CL_UNSIGNED_INT16: return "uint16";
	case CL_UNSIGNED_INT32: return "uint32";
	case CL_HALF_FLOAT: return "half";
	case CL_FLOAT: return "float";

	default: return "Unknown channel data type";
	}
}

////////////////////////////////////////////////////////////////////////////////
cliValue* CreateValue (Pool<>& pool, const char* value)
{
	auto v = pool.Allocate<cliValue> ();

	const auto l = ::strlen (value);

	// Pool will fail if this cast goes wrong
	auto s = pool.Allocate (static_cast<int> (l) + 1);
	::memcpy (s, value, l);
	v->s = static_cast<const char*> (s);

	return v;
}

////////////////////////////////////////////////////////////////////////////////
cliValue* CreateValue (Pool<>& pool, const std::int64_t value)
{
	auto v = pool.Allocate<cliValue> ();
	v->i = value;
	return v;
}

////////////////////////////////////////////////////////////////////////////////
cliValue* CreateValue (Pool<>& pool, const bool value)
{
	auto v = pool.Allocate<cliValue> ();
	v->b = value;
	return v;
}

#define NIV_VALUESTRING(v) v, #v

typedef cliValue* (*CreateFunc)(Pool<>&, void*, std::size_t);

template <typename T>
struct PropertyFetcher
{
	PropertyFetcher (T info, const char* n, CreateFunc cf, cliPropertyType type, const char* h = nullptr)
	: info (info)
	, n (n)
	, cf (cf)
	, type (type)
	, h (h)
	{
	}

	PropertyFetcher () = default;

	T				info;
	const char*		n = nullptr;
	CreateFunc		cf = nullptr;
	cliPropertyType	type;
	const char*		h = nullptr;
};

////////////////////////////////////////////////////////////////////////////////
cliValue* CreateChar (Pool<>& pool, void* buffer, std::size_t)
{
	return CreateValue (pool, static_cast<const char*> (buffer));
}

////////////////////////////////////////////////////////////////////////////////
cliValue* CreateCharList (Pool<>& pool, void* buffer, std::size_t)
{
	char* p = static_cast<char*> (buffer);

	cliValue* result = nullptr;
	cliValue* last = nullptr;

	// This is a string tokenizer, similar to strtok (it will put 0-bytes into
	// the source buffer). We don't use strtok to ensure this is reentrant
	while (*p != '\0') {
		char* s = p;
		while (*p != '\0' && *p != ' ') {
			++p;
		}

		if (*p == ' ') {
			*p = '\0';

			++p;

			// Skip all following spaces
			while (*p == ' ') {
				++p;
			}
		}

		auto value = CreateValue (pool, s);

		if (last) {
			last->next = value;
			last = value;
		} else {
			result = value;
			last = value;
		}
	}

	return result;
}

////////////////////////////////////////////////////////////////////////////////
cliValue* CreateUInt (Pool<>& pool, void* buffer, std::size_t)
{
	return CreateValue (pool, static_cast<std::int64_t> (
		*static_cast<const cl_uint*> (buffer)));
}

////////////////////////////////////////////////////////////////////////////////
cliValue* CreateULong (Pool<>& pool, void* buffer, std::size_t)
{
	return CreateValue (pool, static_cast<std::int64_t> (
		*static_cast<const cl_ulong*> (buffer)));
}

////////////////////////////////////////////////////////////////////////////////
cliValue* CreateSizeT (Pool<>& pool, void* buffer, std::size_t)
{
	return CreateValue (pool, static_cast<std::int64_t> (
		*static_cast<const std::size_t*> (buffer)));
}

////////////////////////////////////////////////////////////////////////////////
cliValue* CreateSizeTList (Pool<>& pool, void* buffer, std::size_t size)
{
	const std::size_t* entries = static_cast<const std::size_t*> (buffer);

	cliValue* result = nullptr;
	cliValue* last = nullptr;

	for (std::size_t i = 0; i < size / sizeof (std::size_t); ++i) {
		cliValue* v = CreateValue (pool, static_cast<std::int64_t> (entries [i]));

		if (last) {
			last->next = v;
			last = v;
		} else {
			result = v;
			last = v;
		}
	}

	return result;
}

////////////////////////////////////////////////////////////////////////////////
cliValue* CreateBool (Pool<>& pool, void* buffer, std::size_t)
{
	return CreateValue (pool, *static_cast<const cl_bool*> (buffer) != 0);
}

template <typename T>
struct BitfieldFetcher
{
	T value;
	const char* n;
};

////////////////////////////////////////////////////////////////////////////////
template <typename T, int Size>
cliValue* CreateBitfield (const T config, const BitfieldFetcher<T> (&fields)[Size], Pool<>& pool)
{
	cliValue* result = nullptr;
	cliValue* lastValue = nullptr;

	for (const auto field : fields) {
		if ((config & field.value) == field.value) {
			auto value = pool.Allocate<cliValue> ();
			value->s = field.n;

			if (lastValue) {
				lastValue->next = value;
				lastValue = lastValue->next;
			} else {
				result = value;
				lastValue = value;
			}
		}
	}

	return result;
}

////////////////////////////////////////////////////////////////////////////////
cliValue* CreateDeviceFPConfig (Pool<>& pool, void* buffer, std::size_t)
{
	const auto config = *static_cast<const cl_device_fp_config*> (buffer);

	static const BitfieldFetcher<cl_device_fp_config> fields [] = {
		{NIV_VALUESTRING (CL_FP_DENORM)},
		{NIV_VALUESTRING (CL_FP_INF_NAN)},
		{NIV_VALUESTRING (CL_FP_ROUND_TO_NEAREST)},
		{NIV_VALUESTRING (CL_FP_ROUND_TO_ZERO)},
		{NIV_VALUESTRING (CL_FP_ROUND_TO_INF)},
		{NIV_VALUESTRING (CL_FP_FMA)},
		{NIV_VALUESTRING (CL_FP_SOFT_FLOAT)}
	};

	return CreateBitfield (config, fields, pool);
}

////////////////////////////////////////////////////////////////////////////////
cliValue* CreateDeviceExecCapabilities (Pool<>& pool, void* buffer, std::size_t)
{
	const auto config = *static_cast<const cl_device_exec_capabilities*> (buffer);

	static const BitfieldFetcher<cl_device_exec_capabilities> fields [] = {
		{NIV_VALUESTRING (CL_EXEC_KERNEL)},
		{NIV_VALUESTRING (CL_EXEC_NATIVE_KERNEL)}
	};

	return CreateBitfield (config, fields, pool);
}

////////////////////////////////////////////////////////////////////////////////
cliValue* CreateDeviceMemCacheType (Pool<>& pool, void* buffer, std::size_t)
{
	const auto config = *static_cast<const cl_device_mem_cache_type*> (buffer);

	static const BitfieldFetcher<cl_device_mem_cache_type> fields [] = {
		// {NIV_VALUESTRING (CL_NONE)},
		{NIV_VALUESTRING (CL_READ_ONLY_CACHE)},
		{NIV_VALUESTRING (CL_READ_WRITE_CACHE)}
	};

	return CreateBitfield (config, fields, pool);
}

////////////////////////////////////////////////////////////////////////////////
cliValue* CreateDeviceLocalMemType (Pool<>& pool, void* buffer, std::size_t)
{
	const auto config = *static_cast<const cl_device_local_mem_type*> (buffer);

	static const BitfieldFetcher<cl_device_local_mem_type> fields [] = {
		// {NIV_VALUESTRING (CL_NONE)},
		{NIV_VALUESTRING (CL_LOCAL)},
		{NIV_VALUESTRING (CL_GLOBAL)}
	};

	return CreateBitfield (config, fields, pool);
}

////////////////////////////////////////////////////////////////////////////////
cliValue* CreateDeviceAffinityDomain (Pool<>& pool, void* buffer, std::size_t)
{
	const auto config = *static_cast<const cl_device_affinity_domain*> (buffer);

	static const BitfieldFetcher<cl_device_affinity_domain> fields [] = {
		{NIV_VALUESTRING (CL_DEVICE_AFFINITY_DOMAIN_NUMA)},
		{NIV_VALUESTRING (CL_DEVICE_AFFINITY_DOMAIN_L4_CACHE)},
		{NIV_VALUESTRING (CL_DEVICE_AFFINITY_DOMAIN_L3_CACHE)},
		{NIV_VALUESTRING (CL_DEVICE_AFFINITY_DOMAIN_L2_CACHE)},
		{NIV_VALUESTRING (CL_DEVICE_AFFINITY_DOMAIN_L1_CACHE)},
		{NIV_VALUESTRING (CL_DEVICE_AFFINITY_DOMAIN_NEXT_PARTITIONABLE)}
	};

	return CreateBitfield (config, fields, pool);
}

////////////////////////////////////////////////////////////////////////////////
cliValue* CreateDevicePartitionProperty (Pool<>& pool, void* buffer, std::size_t)
{
	const auto config = *static_cast<const cl_device_partition_property*> (buffer);

	static const BitfieldFetcher<cl_device_partition_property> fields [] = {
		{NIV_VALUESTRING (CL_DEVICE_PARTITION_EQUALLY)},
		{NIV_VALUESTRING (CL_DEVICE_PARTITION_BY_COUNTS)},
		{NIV_VALUESTRING (CL_DEVICE_PARTITION_BY_AFFINITY_DOMAIN)}
	};

	return CreateBitfield (config, fields, pool);
}

////////////////////////////////////////////////////////////////////////////////
cliValue* CreateCommandQueueProperties (Pool<>& pool, void* buffer, std::size_t)
{
	const auto config = *static_cast<const cl_command_queue_properties*> (buffer);

	static const BitfieldFetcher<cl_command_queue_properties> fields [] = {
		{NIV_VALUESTRING (CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE)},
		{NIV_VALUESTRING (CL_QUEUE_PROFILING_ENABLE)}
	};

	return CreateBitfield (config, fields, pool);
}

////////////////////////////////////////////////////////////////////////////////
cliValue* CreateDeviceType (Pool<>& pool, void* buffer, std::size_t)
{
	const auto config = *static_cast<const cl_device_type*> (buffer);

	static const BitfieldFetcher<cl_device_type> fields [] = {
		{NIV_VALUESTRING (CL_DEVICE_TYPE_CPU)},
		{NIV_VALUESTRING (CL_DEVICE_TYPE_GPU)},
		{NIV_VALUESTRING (CL_DEVICE_TYPE_ACCELERATOR)},
		{NIV_VALUESTRING (CL_DEVICE_TYPE_DEFAULT)},
		{NIV_VALUESTRING (CL_DEVICE_TYPE_CUSTOM)}
	};

	return CreateBitfield (config, fields, pool);
}

#ifdef CL_VERSION_2_0
////////////////////////////////////////////////////////////////////////////////
cliValue* CreateDeviceSVMCapabilities (Pool<>& pool, void* buffer, std::size_t)
{
	const auto config = *static_cast<const cl_device_svm_capabilities*> (buffer);

	static const BitfieldFetcher<cl_device_svm_capabilities> fields [] = {
		{NIV_VALUESTRING (CL_DEVICE_SVM_COARSE_GRAIN_BUFFER)},
		{NIV_VALUESTRING (CL_DEVICE_SVM_FINE_GRAIN_BUFFER)},
		{NIV_VALUESTRING (CL_DEVICE_SVM_FINE_GRAIN_SYSTEM)},
		{NIV_VALUESTRING (CL_DEVICE_SVM_ATOMICS)}
	};

	return CreateBitfield (config, fields, pool);
}
#endif

////////////////////////////////////////////////////////////////////////////////
template <typename GetInfoFunction, typename CLObject, typename Info, typename CreateFunction>
cliValue* GetValue (GetInfoFunction getInfoFunction, CLObject clObject, Info info,
	Pool<>& pool, CreateFunction createFunction)
{
	size_t size;
	NIV_SAFE_CL (getInfoFunction (clObject, info, 0, nullptr, &size));

	if (size == 0) {
		return nullptr;
	}

	// Won't win speed records, but will do
	std::vector<unsigned char> buffer (size);
	NIV_SAFE_CL (getInfoFunction (clObject, info, size, buffer.data (), nullptr));

	return createFunction (pool, buffer.data (), size);
}

////////////////////////////////////////////////////////////////////////////////
template <typename F, typename P, typename Container>
void GetProperties (Pool<>& pool, cliNode* cliNode, F f, P clObject,
	const Container& container)
{
	auto lastProperty = cliNode->firstProperty;

	// If the cliNode already has a property, skip until the end of the property
	// list
	while (lastProperty && lastProperty->next) {
		lastProperty = lastProperty->next;
	}

	for (const auto info : container) {
		auto property = pool.Allocate<cliProperty> ();
		property->type = info.type;
		property->name = info.n;
		property->hint = info.h;
		property->value = GetValue (f, clObject, info.info, pool, info.cf);

		if (lastProperty) {
			lastProperty->next = property;
			lastProperty = lastProperty->next;
		} else {
			cliNode->firstProperty = property;
			lastProperty = property;
		}
	}
}

////////////////////////////////////////////////////////////////////////////////
cliNode* GatherContextInfo (cl_context ctx, Pool<>& pool, const Version clVersion)
{
	struct ImageType
	{
		cl_mem_object_type type;
		const char* n;
	};

	static const std::vector<ImageType> types_CL_1_0 = {
		{CL_MEM_OBJECT_IMAGE1D, "Image1D"},
		{CL_MEM_OBJECT_IMAGE2D, "Image2D"},
		{CL_MEM_OBJECT_IMAGE3D, "Image3D"}
	};

#ifdef CL_VERSION_1_2
	static const std::vector<ImageType> types_CL_1_2 = {
		{CL_MEM_OBJECT_IMAGE1D_BUFFER, "Image1DBuffer"},
		{CL_MEM_OBJECT_IMAGE1D_ARRAY, "Image1DArray"},
		{CL_MEM_OBJECT_IMAGE2D_ARRAY, "Image2DArray"}
	};
#endif

	std::vector<ImageType> types = types_CL_1_0;

#ifdef CL_VERSION_1_2
	if (clVersion >= Version (1, 2)) {
		types.insert (types.end (), types_CL_1_2.begin (), types_CL_1_2.end ());
	}
#endif

	auto imageFormatsNode = pool.Allocate<cliNode> ();
	imageFormatsNode->name = "ImageFormats";

	cliNode* lastImageFormatNode = nullptr;
	for (const auto t : types) {
		auto imageFormatNode = pool.Allocate <cliNode> ();
		imageFormatNode->kind = t.n;
		imageFormatNode->name = "ObjectType";

		cl_uint numImageFormats;
		NIV_SAFE_CL (clGetSupportedImageFormats (ctx, CL_MEM_READ_WRITE, t.type,
			0, nullptr, &numImageFormats));

		if (numImageFormats == 0) {
			continue;
		}

		std::vector<cl_image_format> formats (numImageFormats);
		NIV_SAFE_CL (clGetSupportedImageFormats (ctx, CL_MEM_READ_WRITE, t.type,
			numImageFormats, formats.data (), 0));

		cliNode* lastFormat = nullptr;
		for (const auto format : formats) {
			cliNode* formatcliNode = pool.Allocate<cliNode> ();
			formatcliNode->name = "Format";

			cliProperty* channelOrderProperty = pool.Allocate<cliProperty> ();
			channelOrderProperty->name = "ChannelOrder";
			channelOrderProperty->type = CLI_PropertyType_String;
			channelOrderProperty->value = CreateValue (pool,
				ChannelOrderToString (format.image_channel_order));

			cliProperty* channelDataTypeProperty = pool.Allocate<cliProperty> ();
			channelDataTypeProperty->name = "ChannelDataType";
			channelDataTypeProperty->type = CLI_PropertyType_String;
			channelDataTypeProperty->value = CreateValue (pool,
				ChannelDataTypeToString (format.image_channel_data_type));

			channelOrderProperty->next = channelDataTypeProperty;
			formatcliNode->firstProperty = channelOrderProperty;

			if (lastFormat) {
				lastFormat->next = formatcliNode;
				lastFormat = lastFormat->next;
			} else {
				imageFormatNode->firstChild = formatcliNode;
				lastFormat = formatcliNode;
			}
		}

		if (lastImageFormatNode) {
			lastImageFormatNode->next = imageFormatNode;
			lastImageFormatNode = lastImageFormatNode->next;
		} else {
			imageFormatsNode->firstChild = imageFormatNode;
			lastImageFormatNode = imageFormatNode;
		}
	}

	return imageFormatsNode;
}

////////////////////////////////////////////////////////////////////////////////
cliNode* GatherDeviceInfo (cl_device_id id, Pool<>& pool)
{
	// Unused properties
	// {NIV_VALUESTRING (CL_DEVICE_PARENT_DEVICE), CreateChar, CLI_PropertyType_String},
	// {NIV_VALUESTRING (CL_DEVICE_PLATFORM), CreateUInt, CLI_PropertyType_Int64},

	static const std::vector<PropertyFetcher<cl_device_info>> infos_CL_shared = {
		{NIV_VALUESTRING (CL_DEVICE_ADDRESS_BITS), CreateUInt, CLI_PropertyType_Int64, "The default compute device address space size specified as an unsigned integer value in bits."},
		{NIV_VALUESTRING (CL_DEVICE_AVAILABLE), CreateBool, CLI_PropertyType_Bool},
		{NIV_VALUESTRING (CL_DEVICE_COMPILER_AVAILABLE), CreateBool, CLI_PropertyType_Bool},
		{NIV_VALUESTRING (CL_DEVICE_DOUBLE_FP_CONFIG), CreateDeviceFPConfig, CLI_PropertyType_String},
		{NIV_VALUESTRING (CL_DEVICE_ENDIAN_LITTLE), CreateBool, CLI_PropertyType_Bool},
		{NIV_VALUESTRING (CL_DEVICE_ERROR_CORRECTION_SUPPORT), CreateBool, CLI_PropertyType_Bool},
		{NIV_VALUESTRING (CL_DEVICE_EXECUTION_CAPABILITIES), CreateDeviceExecCapabilities, CLI_PropertyType_String},
		{NIV_VALUESTRING (CL_DEVICE_EXTENSIONS), CreateCharList, CLI_PropertyType_String},
		{NIV_VALUESTRING (CL_DEVICE_GLOBAL_MEM_CACHE_SIZE), CreateULong, CLI_PropertyType_Int64, "Size of global memory cache in bytes."},
		{NIV_VALUESTRING (CL_DEVICE_GLOBAL_MEM_CACHE_TYPE), CreateDeviceMemCacheType, CLI_PropertyType_String},
		{NIV_VALUESTRING (CL_DEVICE_GLOBAL_MEM_CACHELINE_SIZE), CreateUInt, CLI_PropertyType_Int64, "Size of global memory cache line in bytes."},
		{NIV_VALUESTRING (CL_DEVICE_GLOBAL_MEM_SIZE), CreateULong, CLI_PropertyType_Int64, "Size of global device memory in bytes."},
		{NIV_VALUESTRING (CL_DEVICE_IMAGE2D_MAX_HEIGHT), CreateSizeT, CLI_PropertyType_Int64},
		{NIV_VALUESTRING (CL_DEVICE_IMAGE2D_MAX_WIDTH), CreateSizeT, CLI_PropertyType_Int64},
		{NIV_VALUESTRING (CL_DEVICE_IMAGE3D_MAX_DEPTH), CreateSizeT, CLI_PropertyType_Int64},
		{NIV_VALUESTRING (CL_DEVICE_IMAGE3D_MAX_HEIGHT), CreateSizeT, CLI_PropertyType_Int64},
		{NIV_VALUESTRING (CL_DEVICE_IMAGE3D_MAX_WIDTH), CreateSizeT, CLI_PropertyType_Int64},
		{NIV_VALUESTRING (CL_DEVICE_IMAGE_SUPPORT), CreateBool, CLI_PropertyType_Bool},
		{NIV_VALUESTRING (CL_DEVICE_LOCAL_MEM_SIZE), CreateULong, CLI_PropertyType_Int64, "Size of local memory arena in bytes. The minimum value is 32 KB for devices that are not of type CL_DEVICE_TYPE_CUSTOM."},
		{NIV_VALUESTRING (CL_DEVICE_LOCAL_MEM_TYPE), CreateDeviceLocalMemType, CLI_PropertyType_String, "Type of local memory supported. This can be set to CL_LOCAL implying dedicated local memory storage such as SRAM, or CL_GLOBAL. For custom devices, CL_NONE can also be returned indicating no local memory support."},
		{NIV_VALUESTRING (CL_DEVICE_MAX_CLOCK_FREQUENCY), CreateUInt, CLI_PropertyType_Int64, "Maximum configured clock frequency of the device in MHz."},
		{NIV_VALUESTRING (CL_DEVICE_MAX_COMPUTE_UNITS), CreateUInt, CLI_PropertyType_Int64, "The number of parallel compute units on the OpenCL device. A work-group executes on a single compute unit. The minimum value is 1."},
		{NIV_VALUESTRING (CL_DEVICE_MAX_CONSTANT_ARGS), CreateUInt, CLI_PropertyType_Int64},
		{NIV_VALUESTRING (CL_DEVICE_MAX_CONSTANT_BUFFER_SIZE), CreateULong, CLI_PropertyType_Int64, "Max size in bytes of a constant buffer allocation. The minimum value is 64 KB for devices that are not of type CL_DEVICE_TYPE_CUSTOM."},
		{NIV_VALUESTRING (CL_DEVICE_MAX_MEM_ALLOC_SIZE), CreateULong, CLI_PropertyType_Int64, "Max size of memory object allocation in bytes. The minimum value is max (1/4th of CL_DEVICE_GLOBAL_MEM_SIZE, 128*1024*1024) for devices that are not of type CL_DEVICE_TYPE_CUSTOM."},
		{NIV_VALUESTRING (CL_DEVICE_MAX_PARAMETER_SIZE), CreateSizeT, CLI_PropertyType_Int64, "Max size in bytes of the arguments that can be passed to a kernel. The minimum value is 1024 for devices that are not of type CL_DEVICE_TYPE_CUSTOM. For this minimum value, only a maximum of 128 arguments can be passed to a kernel."},
		{NIV_VALUESTRING (CL_DEVICE_MAX_READ_IMAGE_ARGS), CreateUInt, CLI_PropertyType_Int64},
		{NIV_VALUESTRING (CL_DEVICE_MAX_SAMPLERS), CreateUInt, CLI_PropertyType_Int64},
		{NIV_VALUESTRING (CL_DEVICE_MAX_WORK_GROUP_SIZE), CreateSizeT, CLI_PropertyType_Int64},
		{NIV_VALUESTRING (CL_DEVICE_MAX_WORK_ITEM_DIMENSIONS), CreateUInt, CLI_PropertyType_Int64},
		{NIV_VALUESTRING (CL_DEVICE_MAX_WORK_ITEM_SIZES), CreateSizeTList, CLI_PropertyType_Int64},
		{NIV_VALUESTRING (CL_DEVICE_MAX_WRITE_IMAGE_ARGS), CreateUInt, CLI_PropertyType_Int64},
		{NIV_VALUESTRING (CL_DEVICE_MEM_BASE_ADDR_ALIGN), CreateUInt, CLI_PropertyType_Int64},
		{NIV_VALUESTRING (CL_DEVICE_NAME), CreateChar, CLI_PropertyType_String},
		{NIV_VALUESTRING (CL_DEVICE_OPENCL_C_VERSION), CreateChar, CLI_PropertyType_String},
		{NIV_VALUESTRING (CL_DEVICE_PREFERRED_VECTOR_WIDTH_CHAR), CreateUInt, CLI_PropertyType_Int64},
		{NIV_VALUESTRING (CL_DEVICE_PREFERRED_VECTOR_WIDTH_SHORT), CreateUInt, CLI_PropertyType_Int64},
		{NIV_VALUESTRING (CL_DEVICE_PREFERRED_VECTOR_WIDTH_INT), CreateUInt, CLI_PropertyType_Int64},
		{NIV_VALUESTRING (CL_DEVICE_PREFERRED_VECTOR_WIDTH_LONG), CreateUInt, CLI_PropertyType_Int64},
		{NIV_VALUESTRING (CL_DEVICE_PREFERRED_VECTOR_WIDTH_FLOAT), CreateUInt, CLI_PropertyType_Int64},
		{NIV_VALUESTRING (CL_DEVICE_PREFERRED_VECTOR_WIDTH_DOUBLE), CreateUInt, CLI_PropertyType_Int64},
		{NIV_VALUESTRING (CL_DEVICE_PREFERRED_VECTOR_WIDTH_HALF), CreateUInt, CLI_PropertyType_Int64},
		{NIV_VALUESTRING (CL_DEVICE_PROFILE), CreateChar, CLI_PropertyType_String},
		{NIV_VALUESTRING (CL_DEVICE_PROFILING_TIMER_RESOLUTION), CreateSizeT, CLI_PropertyType_Int64},
		{NIV_VALUESTRING (CL_DEVICE_SINGLE_FP_CONFIG), CreateDeviceFPConfig, CLI_PropertyType_String},
		{NIV_VALUESTRING (CL_DEVICE_TYPE), CreateDeviceType, CLI_PropertyType_String},
		{NIV_VALUESTRING (CL_DEVICE_VENDOR), CreateChar, CLI_PropertyType_String},
		{NIV_VALUESTRING (CL_DEVICE_VENDOR_ID), CreateUInt, CLI_PropertyType_Int64},
		{NIV_VALUESTRING (CL_DEVICE_VERSION), CreateChar, CLI_PropertyType_String},
		{NIV_VALUESTRING (CL_DRIVER_VERSION), CreateChar, CLI_PropertyType_String}
	};

#ifdef CL_VERSION_1_0
	// Diff against CL_shared
	static const std::vector<PropertyFetcher<cl_device_info>> infos_CL_1_0 = {
		{NIV_VALUESTRING (CL_DEVICE_MEM_BASE_ADDR_ALIGN), CreateUInt, CLI_PropertyType_Int64}
	};
#endif

#ifdef CL_VERSION_1_1
	// Diff against CL_shared
	static const std::vector<PropertyFetcher<cl_device_info>> infos_CL_1_1 = {
		{NIV_VALUESTRING (CL_DEVICE_HOST_UNIFIED_MEMORY), CreateBool, CLI_PropertyType_Bool},
		{NIV_VALUESTRING (CL_DEVICE_MIN_DATA_TYPE_ALIGN_SIZE), CreateUInt, CLI_PropertyType_Int64},
		{NIV_VALUESTRING (CL_DEVICE_NATIVE_VECTOR_WIDTH_CHAR), CreateUInt, CLI_PropertyType_Int64},
		{NIV_VALUESTRING (CL_DEVICE_NATIVE_VECTOR_WIDTH_SHORT), CreateUInt, CLI_PropertyType_Int64},
		{NIV_VALUESTRING (CL_DEVICE_NATIVE_VECTOR_WIDTH_INT), CreateUInt, CLI_PropertyType_Int64},
		{NIV_VALUESTRING (CL_DEVICE_NATIVE_VECTOR_WIDTH_LONG), CreateUInt, CLI_PropertyType_Int64},
		{NIV_VALUESTRING (CL_DEVICE_NATIVE_VECTOR_WIDTH_FLOAT), CreateUInt, CLI_PropertyType_Int64},
		{NIV_VALUESTRING (CL_DEVICE_NATIVE_VECTOR_WIDTH_DOUBLE), CreateUInt, CLI_PropertyType_Int64},
		{NIV_VALUESTRING (CL_DEVICE_NATIVE_VECTOR_WIDTH_HALF), CreateUInt, CLI_PropertyType_Int64},
	};
#endif

#ifdef CL_VERSION_1_2
	// Diff against CL_shared
	static const std::vector<PropertyFetcher<cl_device_info>> infos_CL_1_2 = {
		{NIV_VALUESTRING (CL_DEVICE_BUILT_IN_KERNELS), CreateCharList, CLI_PropertyType_String},
		{NIV_VALUESTRING (CL_DEVICE_IMAGE_BASE_ADDRESS_ALIGNMENT), CreateUInt, CLI_PropertyType_Int64},
		{NIV_VALUESTRING (CL_DEVICE_IMAGE_MAX_ARRAY_SIZE), CreateSizeT, CLI_PropertyType_Int64},
		{NIV_VALUESTRING (CL_DEVICE_IMAGE_MAX_BUFFER_SIZE), CreateSizeT, CLI_PropertyType_Int64},
		{NIV_VALUESTRING (CL_DEVICE_IMAGE_PITCH_ALIGNMENT), CreateUInt, CLI_PropertyType_Int64},
		{NIV_VALUESTRING (CL_DEVICE_IMAGE_SUPPORT), CreateBool, CLI_PropertyType_Bool},
		{NIV_VALUESTRING (CL_DEVICE_LINKER_AVAILABLE), CreateBool, CLI_PropertyType_Bool},
		{NIV_VALUESTRING (CL_DEVICE_NATIVE_VECTOR_WIDTH_CHAR), CreateUInt, CLI_PropertyType_Int64},
		{NIV_VALUESTRING (CL_DEVICE_NATIVE_VECTOR_WIDTH_SHORT), CreateUInt, CLI_PropertyType_Int64},
		{NIV_VALUESTRING (CL_DEVICE_NATIVE_VECTOR_WIDTH_INT), CreateUInt, CLI_PropertyType_Int64},
		{NIV_VALUESTRING (CL_DEVICE_NATIVE_VECTOR_WIDTH_LONG), CreateUInt, CLI_PropertyType_Int64},
		{NIV_VALUESTRING (CL_DEVICE_NATIVE_VECTOR_WIDTH_FLOAT), CreateUInt, CLI_PropertyType_Int64},
		{NIV_VALUESTRING (CL_DEVICE_NATIVE_VECTOR_WIDTH_DOUBLE), CreateUInt, CLI_PropertyType_Int64},
		{NIV_VALUESTRING (CL_DEVICE_NATIVE_VECTOR_WIDTH_HALF), CreateUInt, CLI_PropertyType_Int64},
		{NIV_VALUESTRING (CL_DEVICE_PARTITION_AFFINITY_DOMAIN), CreateDeviceAffinityDomain, CLI_PropertyType_String},
		{NIV_VALUESTRING (CL_DEVICE_PARTITION_MAX_SUB_DEVICES), CreateUInt, CLI_PropertyType_Int64},
		{NIV_VALUESTRING (CL_DEVICE_PARTITION_PROPERTIES), CreateDevicePartitionProperty, CLI_PropertyType_String},
		{NIV_VALUESTRING (CL_DEVICE_PARTITION_TYPE), CreateDevicePartitionProperty, CLI_PropertyType_String},
		{NIV_VALUESTRING (CL_DEVICE_PRINTF_BUFFER_SIZE), CreateSizeT, CLI_PropertyType_Int64},
		{NIV_VALUESTRING (CL_DEVICE_QUEUE_PROPERTIES), CreateCommandQueueProperties, CLI_PropertyType_String},
	};
#endif

#ifdef CL_VERSION_2_0
	// Diff against CL_shared+CL_1_2
	static const std::vector<PropertyFetcher<cl_device_info>> infos_CL_2_0 = {
		{NIV_VALUESTRING (CL_DEVICE_GLOBAL_VARIABLE_PREFERRED_TOTAL_SIZE), CreateSizeT, CLI_PropertyType_Int64},
		{NIV_VALUESTRING (CL_DEVICE_MAX_GLOBAL_VARIABLE_SIZE), CreateSizeT, CLI_PropertyType_Int64},
		{NIV_VALUESTRING (CL_DEVICE_MAX_ON_DEVICE_EVENTS), CreateUInt, CLI_PropertyType_Int64},
		{NIV_VALUESTRING (CL_DEVICE_MAX_ON_DEVICE_QUEUES), CreateUInt, CLI_PropertyType_Int64},
		{NIV_VALUESTRING (CL_DEVICE_MAX_PIPE_ARGS), CreateUInt, CLI_PropertyType_Int64},
		{NIV_VALUESTRING (CL_DEVICE_MAX_READ_WRITE_IMAGE_ARGS), CreateUInt, CLI_PropertyType_Int64},
		{NIV_VALUESTRING (CL_DEVICE_PIPE_MAX_ACTIVE_RESERVATIONS), CreateUInt, CLI_PropertyType_Int64},
		{NIV_VALUESTRING (CL_DEVICE_PIPE_MAX_PACKET_SIZE), CreateUInt, CLI_PropertyType_Int64},
		{NIV_VALUESTRING (CL_DEVICE_PREFERRED_GLOBAL_ATOMIC_ALIGNMENT), CreateUInt, CLI_PropertyType_Int64},
		{NIV_VALUESTRING (CL_DEVICE_PREFERRED_INTEROP_USER_SYNC), CreateBool, CLI_PropertyType_Bool},
		{NIV_VALUESTRING (CL_DEVICE_PREFERRED_LOCAL_ATOMIC_ALIGNMENT), CreateUInt, CLI_PropertyType_Int64},
		{NIV_VALUESTRING (CL_DEVICE_PREFERRED_PLATFORM_ATOMIC_ALIGNMENT), CreateUInt, CLI_PropertyType_Int64},
		{NIV_VALUESTRING (CL_DEVICE_QUEUE_ON_DEVICE_MAX_SIZE), CreateUInt, CLI_PropertyType_Int64},
		{NIV_VALUESTRING (CL_DEVICE_QUEUE_ON_DEVICE_PREFERRED_SIZE), CreateUInt, CLI_PropertyType_Int64},
		{NIV_VALUESTRING (CL_DEVICE_QUEUE_ON_DEVICE_PROPERTIES), CreateCommandQueueProperties, CLI_PropertyType_String},
		{NIV_VALUESTRING (CL_DEVICE_QUEUE_ON_HOST_PROPERTIES), CreateCommandQueueProperties, CLI_PropertyType_String},
		{NIV_VALUESTRING (CL_DEVICE_SVM_CAPABILITIES), CreateDeviceSVMCapabilities, CLI_PropertyType_String}
	};

	// Extension properties
	static const std::vector<PropertyFetcher<cl_device_info>> infos_CL_2_0_Ext = {
		// {NIV_VALUESTRING (CL_DEVICE_SPIR_VERSIONS), CreateCharList, CLI_PropertyType_String},
		// {NIV_VALUESTRING (CL_DEVICE_TERMINATE_CAPABILITY_KHR), CreateDeviceTerminateCapability, CLI_PropertyType_String},
	};
#endif
	auto deviceNode = pool.Allocate<cliNode> ();
	deviceNode->name = "Device";

	// Get OpenCL version
	std::size_t versionSize;
	NIV_SAFE_CL (clGetDeviceInfo (id, CL_DEVICE_VERSION, 0, nullptr, &versionSize));

	std::string versionString;
	versionString.resize (versionSize);
	NIV_SAFE_CL (clGetDeviceInfo(id, CL_DEVICE_VERSION, versionSize,
		// Ugly but safe
		const_cast<char*> (versionString.data ()), nullptr));

	const auto version = ParseVersion (versionString.c_str ());

	std::vector<PropertyFetcher<cl_device_info>> propertiesToFetch =
			infos_CL_shared;

	if (version == Version (1, 0)) {
		propertiesToFetch.insert (propertiesToFetch.end (),
			infos_CL_1_0.begin (), infos_CL_1_0.end ());
	}
#if CL_VERSION_1_1
	else if (version == Version (1, 1)) {
		propertiesToFetch.insert (propertiesToFetch.end (),
			infos_CL_1_1.begin (), infos_CL_1_1.end ());
	}
#endif
#if CL_VERSION_1_2
	else if (version == Version (1, 2)) {
		propertiesToFetch.insert (propertiesToFetch.end (),
			infos_CL_1_2.begin (), infos_CL_1_2.end ());
	}
#endif
#if CL_VERSION_2_0
	else if (version >= Version (2, 0)) {
		propertiesToFetch.insert (propertiesToFetch.end (),
			infos_CL_1_2.begin (), infos_CL_1_2.end ());
		propertiesToFetch.insert (propertiesToFetch.end (),
			infos_CL_2_0.begin (), infos_CL_2_0.end ());
	}
#endif

	// Sort by name
	std::sort (propertiesToFetch.begin (), propertiesToFetch.end (),
			   [](const PropertyFetcher<cl_device_info>& a,
				  const PropertyFetcher<cl_device_info>& b) {
					return ::strcmp (a.n, b.n) < 0;
	});

	GetProperties (pool, deviceNode, clGetDeviceInfo, id, propertiesToFetch);

	cl_int result;
	auto ctx = clCreateContext (nullptr, 1, &id, nullptr, nullptr, &result);

	if (result == CL_SUCCESS) {
		deviceNode->firstChild = GatherContextInfo (ctx, pool, version);
	}

	clReleaseContext (ctx);

	return deviceNode;
}

////////////////////////////////////////////////////////////////////////////////
cliNode* GatherOpenCLInfo (Pool<>& pool)
{
	auto rootNode = pool.Allocate <cliNode> ();
	rootNode->name = "Platforms";

	cl_uint numPlatforms;
	NIV_SAFE_CL(clGetPlatformIDs (0, NULL, &numPlatforms));

	if (numPlatforms <= 0) {
		std::cerr << "Failed to find any OpenCL platform." << std::endl;
		return nullptr;
	}

	std::vector<cl_platform_id> platformIds (numPlatforms);
	NIV_SAFE_CL(clGetPlatformIDs (numPlatforms, platformIds.data (), nullptr));

	cliNode* lastPlatformNode = nullptr;
	for (const auto platformId : platformIds) {
		auto platformNode = pool.Allocate <cliNode> ();

		if (rootNode->firstChild) {
			lastPlatformNode->next = platformNode;
			lastPlatformNode = lastPlatformNode->next;
		} else {
			rootNode->firstChild = platformNode;
			lastPlatformNode = platformNode;
		}

		platformNode->name = "Platform";

		static const std::vector<PropertyFetcher<cl_platform_info>> infos = {
			{ NIV_VALUESTRING (CL_PLATFORM_PROFILE), CreateChar, CLI_PropertyType_String},
			{ NIV_VALUESTRING (CL_PLATFORM_VERSION), CreateChar, CLI_PropertyType_String},
			{ NIV_VALUESTRING (CL_PLATFORM_NAME), CreateChar, CLI_PropertyType_String},
			{ NIV_VALUESTRING (CL_PLATFORM_VENDOR), CreateChar, CLI_PropertyType_String},
			{ NIV_VALUESTRING (CL_PLATFORM_EXTENSIONS), CreateCharList, CLI_PropertyType_String}
		};

		GetProperties (pool, platformNode, clGetPlatformInfo, platformId, infos);

		cl_uint numDevices;
		NIV_SAFE_CL (clGetDeviceIDs (platformId, CL_DEVICE_TYPE_ALL,
			0, nullptr, &numDevices));
		std::vector<cl_device_id> deviceIds (numDevices);
		NIV_SAFE_CL (clGetDeviceIDs (platformId, CL_DEVICE_TYPE_ALL,
			numDevices, deviceIds.data (), 0));

		cliNode* last = nullptr;

		auto devicesNode = pool.Allocate <cliNode> ();
		devicesNode->name = "Devices";
		platformNode->firstChild = devicesNode;

		for (const auto deviceId : deviceIds) {
			auto deviceNode = GatherDeviceInfo (deviceId, pool);

			if (last) {
				last->next = deviceNode;
				last = last->next;
			} else {
				devicesNode->firstChild = deviceNode;
				last = deviceNode;
			}
		}
	}

	return rootNode;
}
}

////////////////////////////////////////////////////////////////////////////////
struct cliInfo
{
	Pool<>			pool;
	struct cliNode*	root = nullptr;
};

////////////////////////////////////////////////////////////////////////////////
int cliInfo_Create (cliInfo** info)
{
	if (info == nullptr) {
		return CLI_Error;
	}

	*info = new cliInfo;

	return CLI_Success;
}

////////////////////////////////////////////////////////////////////////////////
int cliInfo_Gather (cliInfo* info)
{
	if (info->root) {
		return CLI_Error;
	}

	try {
		info->root = GatherOpenCLInfo (info->pool);
	} catch (const std::exception&) {
		return CLI_Error;
	}

	return CLI_Success;
}

////////////////////////////////////////////////////////////////////////////////
int cliInfo_GetRoot (const cliInfo* info, cliNode** root)
{
	if (info == nullptr) {
		return CLI_Error;
	}

	if (info->root == nullptr) {
		return CLI_Error;
	}

	if (root == nullptr) {
		return CLI_Error;
	}

	*root = info->root;

	return CLI_Success;
}

////////////////////////////////////////////////////////////////////////////////
int cliInfo_Destroy (cliInfo* info)
{
	delete info;

	return CLI_Success;
}
