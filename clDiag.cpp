// Matthäus G. Chajdas
// Licensed under the 3-clause BSD license

#include <iostream>
#include <sstream>
#include <vector>
#include <iterator>

#include <list>
#include <vector>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include <cassert>

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

/**
Holds a property value. If next is not null, there are more values for this
property.
*/
struct Value
{
	union {
		std::int64_t	i;
		bool			b;
		const char*		s;
	};

	Value*	next;
};

/**
A property. May contain one value, in this case, value will be non-zero.
*/
struct Property
{
	enum Type
	{
		PT_INT64,
		PT_BOOL,
		PT_STRING
	};

	Value*	value;

	const char* name;
	Property*	next;
	Type		type;
};

/*
An interior node in the property tree. If firstChild is non-null, there is a
child branch. Sibling nodes can be enumerated using the next pointer. Properties
are present if firstProperty is not null.
*/
struct Node
{
	const char* name;

	Node*		firstChild;
	Node*		next;
	Property*	firstProperty;
};

/**
Dump tree to XML.

The output is unformatted, that is, there is no whitespace between elements.
*/
struct XmlPrinter
{
public:
	void Write (std::ostream& s, const Node* tree) const
	{
		OnNode (s, tree);
	}

private:
	void OnNode (std::ostream& s, const Node* node) const
	{
		s << "<" << node->name << ">";

		for (Property* p = node->firstProperty; p; p = p->next) {
			OnProperty (s, p);
		}

		for (Node* n = node->firstChild; n; n = n->next) {
			OnNode (s, n);
		}

		s << "</" << node->name << ">";
	}

	void OnProperty (std::ostream& s, const Property* p) const
	{
		const char* t = nullptr;
		switch (p->type) {
		case Property::PT_BOOL: t = "bool"; break;
		case Property::PT_INT64: t = "int64"; break;
		case Property::PT_STRING: t = "string"; break;
		}

		s << "<Property Name=\"" << p->name << "\" Type=\"" << t << "\">";

		for (Value* v = p->value; v; v = v->next) {
			s << "<Value>";

			switch (p->type) {
			case Property::PT_BOOL:
				if (v->b) {
					s << "true";
				} else {
					s << "false";
				}
				break;


			case Property::PT_INT64:
				s << v->i;
				break;

			case Property::PT_STRING:
				s << v->s;
				break;
			}

			s << "</Value>";
		}

		s << "</Property>";
	}
};

/**
Dump tree to JSON.
*/
struct JsonPrinter
{
public:
	void Write (std::ostream& s, const Node* tree) const
	{
		OnNode (s, tree);
	}

private:
	void OnNode (std::ostream& s, const Node* node) const
	{
		s << "{ \"" << node->name << "\" : {";
		s << "\"Properties\" : ";

		if (node->firstProperty) {
			for (Property* p = node->firstProperty; p; p = p->next) {
				OnProperty (s, p);
				if (p->next) {
					s << ",";
				}
			}
		} else {
			s << "{}";
		}

		s << ", \"Children\" : ";
		if (node->firstChild) {
			for (Node* n = node->firstChild; n; n = n->next) {
				OnNode (s, n);
				if (n->next) {
					s << ",";
				}
			}
		} else {
			s << "{}";
		}

		s << "}";
	}

	void OnProperty (std::ostream& s, const Property* p) const
	{
		bool singleValue = (p->value && p->value->next == nullptr);

		if (!singleValue) {
			s << "\"" << p->name << "\" = [";
		} else {
			s << "\"" << p->name << "\" = ";
		}

		for (Value* v = p->value; v; v = v->next) {
			switch (p->type) {
			case Property::PT_BOOL:
				if (v->b) {
					s << "true";
				} else {
					s << "false";
				}
				break;


			case Property::PT_INT64:
				s << v->i;
				break;

			case Property::PT_STRING:
				s << "\"" << v->s << "\"";
				break;
			}

			if (v->next) {
				s << ",";
			}
		}

		if (! singleValue) {
			s << "]";
		}
	}
};

// For image_format output
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

// for image format output
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
	default: return "Unknown data type";
	}
}

////////////////////////////////////////////////////////////////////////////////
Value* CreateValue (Pool<>& pool, const char* value)
{
	Value* v = pool.Allocate<Value> ();

	const auto l = ::strlen (value);

	// Pool will fail if this cast goes wrong
	auto s = pool.Allocate (static_cast<int> (l) + 1);
	::memcpy (s, value, l);
	v->s = static_cast<const char*> (s);

	return v;
}

////////////////////////////////////////////////////////////////////////////////
Value* CreateValue (Pool<>& pool, const std::int64_t value)
{
	Value* v = pool.Allocate<Value> ();
	v->i = value;
	return v;
}

////////////////////////////////////////////////////////////////////////////////
Value* CreateValue (Pool<>& pool, const bool value)
{
	Value* v = pool.Allocate<Value> ();
	v->b = value;
	return v;
}

#define NIV_VALUESTRING(v) v, #v

typedef Value* (*CreateFunc)(Pool<>&, void*, std::size_t);

template <typename T>
struct PropertyFetcher
{
	T info;
	const char* n;
	CreateFunc cf;
	Property::Type type;
};

////////////////////////////////////////////////////////////////////////////////
Value* CreateChar (Pool<>& pool, void* buffer, std::size_t)
{
	return CreateValue (pool, static_cast<const char*> (buffer));
}

////////////////////////////////////////////////////////////////////////////////
Value* CreateCharList (Pool<>& pool, void* buffer, std::size_t)
{
	char* p = static_cast<char*> (buffer);

	Value* result = nullptr;
	Value* last = nullptr;

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
		}

		Value* v = CreateValue (pool, s);

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
Value* CreateUInt (Pool<>& pool, void* buffer, std::size_t)
{
	return CreateValue (pool, static_cast<std::int64_t> (
		*static_cast<const cl_uint*> (buffer)));
}

////////////////////////////////////////////////////////////////////////////////
Value* CreateULong (Pool<>& pool, void* buffer, std::size_t)
{
	return CreateValue (pool, static_cast<std::int64_t> (
		*static_cast<const cl_ulong*> (buffer)));
}

////////////////////////////////////////////////////////////////////////////////
Value* CreateSizeT (Pool<>& pool, void* buffer, std::size_t)
{
	return CreateValue (pool, static_cast<std::int64_t> (
		*static_cast<const std::size_t*> (buffer)));
}

////////////////////////////////////////////////////////////////////////////////
Value* CreateSizeTList (Pool<>& pool, void* buffer, std::size_t size)
{
	const std::size_t* entries = static_cast<const std::size_t*> (buffer);

	Value* result = nullptr;
	Value* last = nullptr;

	for (std::size_t i = 0; i < size / sizeof (std::size_t); ++i) {
		Value* v = CreateValue (pool, static_cast<std::int64_t> (entries [i]));

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
Value* CreateBool (Pool<>& pool, void* buffer, std::size_t)
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
Value* CreateBitfield (const T config, const BitfieldFetcher<T> (&fields)[Size], Pool<>& pool)
{
	Value* result = nullptr;
	Value* lastValue = nullptr;

	for (const auto field : fields) {
		if ((config & field.value) == field.value) {
			Value* v = pool.Allocate<Value> ();
			v->s = field.n;

			if (lastValue) {
				lastValue->next = v;
				lastValue = lastValue->next;
			} else {
				result = v;
				lastValue = v;
			}
		}
	}

	return result;
}

////////////////////////////////////////////////////////////////////////////////
Value* CreateDeviceFPConfig (Pool<>& pool, void* buffer, std::size_t)
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
Value* CreateDeviceExecutionCapabilities (Pool<>& pool, void* buffer, std::size_t)
{
	const auto config = *static_cast<const cl_device_exec_capabilities*> (buffer);

	static const BitfieldFetcher<cl_device_exec_capabilities> fields [] = {
		{NIV_VALUESTRING (CL_EXEC_KERNEL)},
		{NIV_VALUESTRING (CL_EXEC_NATIVE_KERNEL)}
	};

	return CreateBitfield (config, fields, pool);
}

////////////////////////////////////////////////////////////////////////////////
Value* CreateDeviceMemCacheType (Pool<>& pool, void* buffer, std::size_t)
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
Value* CreateDeviceLocalMemType (Pool<>& pool, void* buffer, std::size_t)
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
Value* CreateDeviceAffinityDomain (Pool<>& pool, void* buffer, std::size_t)
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
Value* CreateDevicePartitionProperty (Pool<>& pool, void* buffer, std::size_t)
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
Value* CreateCommandQueueProperties (Pool<>& pool, void* buffer, std::size_t)
{
	const auto config = *static_cast<const cl_command_queue_properties*> (buffer);

	static const BitfieldFetcher<cl_command_queue_properties> fields [] = {
		{NIV_VALUESTRING (CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE)},
		{NIV_VALUESTRING (CL_QUEUE_PROFILING_ENABLE)}
	};

	return CreateBitfield (config, fields, pool);
}

////////////////////////////////////////////////////////////////////////////////
Value* CreateDeviceType (Pool<>& pool, void* buffer, std::size_t)
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
Value* CreateDeviceSVMCapabilities (Pool<>& pool, void* buffer, std::size_t)
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
Value* GetValue (GetInfoFunction getInfoFunction, CLObject clObject, Info info,
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
void GetProperties (Pool<>& pool, Node* node, F f, P clObject,
	const Container& c)
{
	Property* lastProperty = node->firstProperty;
	
	// If the node already has a property, skip until the end of the property
	// list
	while (lastProperty && lastProperty->next) {
		lastProperty = lastProperty->next;
	}

	for (const auto info : c) {
		Property* p = pool.Allocate<Property> ();
		p->type = info.type;
		p->name = info.n;
		p->value = GetValue (f, clObject, info.info, pool, info.cf);

		if (lastProperty) {
			lastProperty->next = p;
			lastProperty = lastProperty->next;
		} else {
			node->firstProperty = p;
			lastProperty = p;
		}
	}
}

////////////////////////////////////////////////////////////////////////////////
Node* GatherContextInfo (cl_context ctx, Pool<>& pool, const Version clVersion)
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

	static const std::vector<ImageType> types_CL_1_2 = {
		{CL_MEM_OBJECT_IMAGE1D_BUFFER, "Image1DBuffer"},
		{CL_MEM_OBJECT_IMAGE1D_ARRAY, "Image1DArray"},
		{CL_MEM_OBJECT_IMAGE2D_ARRAY, "Image2DArray"}
	};

	std::vector<ImageType> types = types_CL_1_0;

	if (clVersion >= Version (1, 2)) {
		types.insert (types.end (), types_CL_1_2.begin (), types_CL_1_2.end ());
	}

	Node* imageFormats = pool.Allocate<Node> ();
	imageFormats->name = "ImageFormats";

	Node* lastImageFormat = nullptr;
	for (const auto t : types) {
		Node* imageFormat = pool.Allocate <Node> ();
		imageFormat->name = t.n;

		cl_uint numImageFormats;
		NIV_SAFE_CL (clGetSupportedImageFormats (ctx, CL_MEM_READ_WRITE, t.type,
			0, nullptr, &numImageFormats));

		if (numImageFormats == 0) {
			continue;
		}

		std::vector<cl_image_format> formats (numImageFormats);
		NIV_SAFE_CL (clGetSupportedImageFormats (ctx, CL_MEM_READ_WRITE, t.type,
			numImageFormats, formats.data (), 0));

		Node* lastFormat = nullptr;
		for (const auto format : formats) {
			Node* formatNode = pool.Allocate<Node> ();
			formatNode->name = "Format";

			Property* channelOrderProperty = pool.Allocate<Property> ();
			channelOrderProperty->name = "ChannelOrder";
			channelOrderProperty->type = Property::PT_STRING;
			channelOrderProperty->value = CreateValue (pool,
				ChannelOrderToString (format.image_channel_order));

			Property* channelDataTypeProperty = pool.Allocate<Property> ();
			channelDataTypeProperty->name = "ChannelDataType";
			channelDataTypeProperty->type = Property::PT_STRING;
			channelDataTypeProperty->value = CreateValue (pool,
				ChannelDataTypeToString (format.image_channel_data_type));

			channelOrderProperty->next = channelDataTypeProperty;
			formatNode->firstProperty = channelOrderProperty;

			if (lastFormat) {
				lastFormat->next = formatNode;
				lastFormat = lastFormat->next;
			} else {
				imageFormat->firstChild = formatNode;
				lastFormat = formatNode;
			}
		}

		if (lastImageFormat) {
			lastImageFormat->next = imageFormat;
			lastImageFormat = lastImageFormat->next;
		} else {
			imageFormats->firstChild = imageFormat;
			lastImageFormat = imageFormat;
		}
	}

	return imageFormats;
}

////////////////////////////////////////////////////////////////////////////////
Node* GatherDeviceInfo (cl_device_id id, Pool<>& pool)
{
	// Unused properties
	// {NIV_VALUESTRING (CL_DEVICE_PARENT_DEVICE), CreateChar, Property::PT_STRING},
	// {NIV_VALUESTRING (CL_DEVICE_PLATFORM), CreateUInt, Property::PT_INT64},

#ifdef CL_VERSION_1_0
	static const std::vector<PropertyFetcher<cl_device_info>> infos_CL_1_0 = {
		{NIV_VALUESTRING (CL_DEVICE_ADDRESS_BITS), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_AVAILABLE), CreateBool, Property::PT_BOOL},
		{NIV_VALUESTRING (CL_DEVICE_COMPILER_AVAILABLE), CreateBool, Property::PT_BOOL},
		{NIV_VALUESTRING (CL_DEVICE_DOUBLE_FP_CONFIG), CreateDeviceFPConfig, Property::PT_STRING},
		{NIV_VALUESTRING (CL_DEVICE_ENDIAN_LITTLE), CreateBool, Property::PT_BOOL},
		{NIV_VALUESTRING (CL_DEVICE_ERROR_CORRECTION_SUPPORT), CreateBool, Property::PT_BOOL},
		{NIV_VALUESTRING (CL_DEVICE_EXECUTION_CAPABILITIES), CreateDeviceExecutionCapabilities, Property::PT_STRING},
		{NIV_VALUESTRING (CL_DEVICE_EXTENSIONS), CreateCharList, Property::PT_STRING},
		{NIV_VALUESTRING (CL_DEVICE_GLOBAL_MEM_CACHE_SIZE), CreateULong, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_GLOBAL_MEM_CACHE_TYPE), CreateDeviceMemCacheType, Property::PT_STRING},
		{NIV_VALUESTRING (CL_DEVICE_GLOBAL_MEM_CACHELINE_SIZE), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_GLOBAL_MEM_SIZE), CreateULong, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_IMAGE2D_MAX_HEIGHT), CreateSizeT, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_IMAGE2D_MAX_WIDTH), CreateSizeT, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_IMAGE3D_MAX_DEPTH), CreateSizeT, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_IMAGE3D_MAX_HEIGHT), CreateSizeT, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_IMAGE3D_MAX_WIDTH), CreateSizeT, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_IMAGE_SUPPORT), CreateBool, Property::PT_BOOL},
		{NIV_VALUESTRING (CL_DEVICE_LOCAL_MEM_SIZE), CreateULong, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_LOCAL_MEM_TYPE), CreateDeviceLocalMemType, Property::PT_STRING},
		{NIV_VALUESTRING (CL_DEVICE_MAX_CLOCK_FREQUENCY), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_MAX_COMPUTE_UNITS), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_MAX_CONSTANT_ARGS), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_MAX_CONSTANT_BUFFER_SIZE), CreateULong, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_MAX_MEM_ALLOC_SIZE), CreateULong, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_MAX_PARAMETER_SIZE), CreateSizeT, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_MAX_READ_IMAGE_ARGS), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_MAX_SAMPLERS), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_MAX_WORK_GROUP_SIZE), CreateSizeT, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_MAX_WORK_ITEM_DIMENSIONS), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_MAX_WORK_ITEM_SIZES), CreateSizeTList, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_MAX_WRITE_IMAGE_ARGS), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_MEM_BASE_ADDR_ALIGN), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_MIN_DATA_TYPE_ALIGN_SIZE), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_NAME), CreateChar, Property::PT_STRING},
		{NIV_VALUESTRING (CL_DEVICE_OPENCL_C_VERSION), CreateChar, Property::PT_STRING},
		{NIV_VALUESTRING (CL_DEVICE_PREFERRED_VECTOR_WIDTH_CHAR), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_PREFERRED_VECTOR_WIDTH_SHORT), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_PREFERRED_VECTOR_WIDTH_INT), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_PREFERRED_VECTOR_WIDTH_LONG), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_PREFERRED_VECTOR_WIDTH_FLOAT), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_PREFERRED_VECTOR_WIDTH_DOUBLE), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_PREFERRED_VECTOR_WIDTH_HALF), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_PROFILE), CreateChar, Property::PT_STRING},
		{NIV_VALUESTRING (CL_DEVICE_PROFILING_TIMER_RESOLUTION), CreateSizeT, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_SINGLE_FP_CONFIG), CreateDeviceFPConfig, Property::PT_STRING},
		{NIV_VALUESTRING (CL_DEVICE_TYPE), CreateDeviceType, Property::PT_STRING},
		{NIV_VALUESTRING (CL_DEVICE_VENDOR), CreateChar, Property::PT_STRING},
		{NIV_VALUESTRING (CL_DEVICE_VENDOR_ID), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_VERSION), CreateChar, Property::PT_STRING},
		{NIV_VALUESTRING (CL_DRIVER_VERSION), CreateChar, Property::PT_STRING}
	};
#endif

#ifdef CL_VERSION_1_1
	static const std::vector<PropertyFetcher<cl_device_info>> infos_CL_1_1 = {
		{NIV_VALUESTRING (CL_DEVICE_ADDRESS_BITS), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_AVAILABLE), CreateBool, Property::PT_BOOL},
		{NIV_VALUESTRING (CL_DEVICE_COMPILER_AVAILABLE), CreateBool, Property::PT_BOOL},
		{NIV_VALUESTRING (CL_DEVICE_DOUBLE_FP_CONFIG), CreateDeviceFPConfig, Property::PT_STRING},
		{NIV_VALUESTRING (CL_DEVICE_ENDIAN_LITTLE), CreateBool, Property::PT_BOOL},
		{NIV_VALUESTRING (CL_DEVICE_ERROR_CORRECTION_SUPPORT), CreateBool, Property::PT_BOOL},
		{NIV_VALUESTRING (CL_DEVICE_EXECUTION_CAPABILITIES), CreateDeviceExecutionCapabilities, Property::PT_STRING},
		{NIV_VALUESTRING (CL_DEVICE_EXTENSIONS), CreateCharList, Property::PT_STRING},
		{NIV_VALUESTRING (CL_DEVICE_GLOBAL_MEM_CACHE_SIZE), CreateULong, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_GLOBAL_MEM_CACHE_TYPE), CreateDeviceMemCacheType, Property::PT_STRING},
		{NIV_VALUESTRING (CL_DEVICE_GLOBAL_MEM_CACHELINE_SIZE), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_GLOBAL_MEM_SIZE), CreateULong, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_HOST_UNIFIED_MEMORY), CreateBool, Property::PT_BOOL},
		{NIV_VALUESTRING (CL_DEVICE_IMAGE2D_MAX_HEIGHT), CreateSizeT, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_IMAGE2D_MAX_WIDTH), CreateSizeT, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_IMAGE3D_MAX_DEPTH), CreateSizeT, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_IMAGE3D_MAX_HEIGHT), CreateSizeT, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_IMAGE3D_MAX_WIDTH), CreateSizeT, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_IMAGE_SUPPORT), CreateBool, Property::PT_BOOL},
		{NIV_VALUESTRING (CL_DEVICE_LOCAL_MEM_SIZE), CreateULong, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_LOCAL_MEM_TYPE), CreateDeviceLocalMemType, Property::PT_STRING},
		{NIV_VALUESTRING (CL_DEVICE_MAX_CLOCK_FREQUENCY), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_MAX_COMPUTE_UNITS), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_MAX_CONSTANT_ARGS), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_MAX_CONSTANT_BUFFER_SIZE), CreateULong, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_MAX_MEM_ALLOC_SIZE), CreateULong, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_MAX_PARAMETER_SIZE), CreateSizeT, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_MAX_READ_IMAGE_ARGS), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_MAX_SAMPLERS), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_MAX_WORK_GROUP_SIZE), CreateSizeT, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_MAX_WORK_ITEM_DIMENSIONS), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_MAX_WORK_ITEM_SIZES), CreateSizeTList, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_MAX_WRITE_IMAGE_ARGS), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_MEM_BASE_ADDR_ALIGN), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_MIN_DATA_TYPE_ALIGN_SIZE), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_NAME), CreateChar, Property::PT_STRING},
		{NIV_VALUESTRING (CL_DEVICE_NATIVE_VECTOR_WIDTH_CHAR), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_NATIVE_VECTOR_WIDTH_SHORT), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_NATIVE_VECTOR_WIDTH_INT), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_NATIVE_VECTOR_WIDTH_LONG), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_NATIVE_VECTOR_WIDTH_FLOAT), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_NATIVE_VECTOR_WIDTH_DOUBLE), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_NATIVE_VECTOR_WIDTH_HALF), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_OPENCL_C_VERSION), CreateChar, Property::PT_STRING},
		{NIV_VALUESTRING (CL_DEVICE_PREFERRED_VECTOR_WIDTH_CHAR), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_PREFERRED_VECTOR_WIDTH_SHORT), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_PREFERRED_VECTOR_WIDTH_INT), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_PREFERRED_VECTOR_WIDTH_LONG), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_PREFERRED_VECTOR_WIDTH_FLOAT), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_PREFERRED_VECTOR_WIDTH_DOUBLE), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_PREFERRED_VECTOR_WIDTH_HALF), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_PROFILE), CreateChar, Property::PT_STRING},
		{NIV_VALUESTRING (CL_DEVICE_PROFILING_TIMER_RESOLUTION), CreateSizeT, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_SINGLE_FP_CONFIG), CreateDeviceFPConfig, Property::PT_STRING},
		{NIV_VALUESTRING (CL_DEVICE_TYPE), CreateDeviceType, Property::PT_STRING},
		{NIV_VALUESTRING (CL_DEVICE_VENDOR), CreateChar, Property::PT_STRING},
		{NIV_VALUESTRING (CL_DEVICE_VENDOR_ID), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_VERSION), CreateChar, Property::PT_STRING},
		{NIV_VALUESTRING (CL_DRIVER_VERSION), CreateChar, Property::PT_STRING}
	};
#endif

#ifdef CL_VERSION_1_2
	static const std::vector<PropertyFetcher<cl_device_info>> infos_CL_1_2 = {
		{NIV_VALUESTRING (CL_DEVICE_ADDRESS_BITS), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_AVAILABLE), CreateBool, Property::PT_BOOL},
		{NIV_VALUESTRING (CL_DEVICE_BUILT_IN_KERNELS), CreateCharList, Property::PT_STRING},
		{NIV_VALUESTRING (CL_DEVICE_COMPILER_AVAILABLE), CreateBool, Property::PT_BOOL},
		{NIV_VALUESTRING (CL_DEVICE_DOUBLE_FP_CONFIG), CreateDeviceFPConfig, Property::PT_STRING},
		{NIV_VALUESTRING (CL_DEVICE_ENDIAN_LITTLE), CreateBool, Property::PT_BOOL},
		{NIV_VALUESTRING (CL_DEVICE_ERROR_CORRECTION_SUPPORT), CreateBool, Property::PT_BOOL},
		{NIV_VALUESTRING (CL_DEVICE_EXECUTION_CAPABILITIES), CreateDeviceExecutionCapabilities, Property::PT_STRING},
		{NIV_VALUESTRING (CL_DEVICE_EXTENSIONS), CreateCharList, Property::PT_STRING},
		{NIV_VALUESTRING (CL_DEVICE_GLOBAL_MEM_CACHE_SIZE), CreateULong, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_GLOBAL_MEM_CACHE_TYPE), CreateDeviceMemCacheType, Property::PT_STRING},
		{NIV_VALUESTRING (CL_DEVICE_GLOBAL_MEM_CACHELINE_SIZE), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_GLOBAL_MEM_SIZE), CreateULong, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_IMAGE2D_MAX_HEIGHT), CreateSizeT, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_IMAGE2D_MAX_WIDTH), CreateSizeT, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_IMAGE3D_MAX_DEPTH), CreateSizeT, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_IMAGE3D_MAX_HEIGHT), CreateSizeT, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_IMAGE3D_MAX_WIDTH), CreateSizeT, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_IMAGE_BASE_ADDRESS_ALIGNMENT), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_IMAGE_MAX_ARRAY_SIZE), CreateSizeT, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_IMAGE_MAX_BUFFER_SIZE), CreateSizeT, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_IMAGE_PITCH_ALIGNMENT), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_IMAGE_SUPPORT), CreateBool, Property::PT_BOOL},
		{NIV_VALUESTRING (CL_DEVICE_LINKER_AVAILABLE), CreateBool, Property::PT_BOOL},
		{NIV_VALUESTRING (CL_DEVICE_LOCAL_MEM_SIZE), CreateULong, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_LOCAL_MEM_TYPE), CreateDeviceLocalMemType, Property::PT_STRING},
		{NIV_VALUESTRING (CL_DEVICE_MAX_CLOCK_FREQUENCY), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_MAX_COMPUTE_UNITS), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_MAX_CONSTANT_ARGS), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_MAX_CONSTANT_BUFFER_SIZE), CreateULong, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_MAX_MEM_ALLOC_SIZE), CreateULong, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_MAX_PARAMETER_SIZE), CreateSizeT, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_MAX_READ_IMAGE_ARGS), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_MAX_SAMPLERS), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_MAX_WORK_GROUP_SIZE), CreateSizeT, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_MAX_WORK_ITEM_DIMENSIONS), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_MAX_WORK_ITEM_SIZES), CreateSizeTList, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_MAX_WRITE_IMAGE_ARGS), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_MEM_BASE_ADDR_ALIGN), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_NAME), CreateChar, Property::PT_STRING},
		{NIV_VALUESTRING (CL_DEVICE_NATIVE_VECTOR_WIDTH_CHAR), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_NATIVE_VECTOR_WIDTH_SHORT), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_NATIVE_VECTOR_WIDTH_INT), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_NATIVE_VECTOR_WIDTH_LONG), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_NATIVE_VECTOR_WIDTH_FLOAT), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_NATIVE_VECTOR_WIDTH_DOUBLE), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_NATIVE_VECTOR_WIDTH_HALF), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_OPENCL_C_VERSION), CreateChar, Property::PT_STRING},
		{NIV_VALUESTRING (CL_DEVICE_PARTITION_AFFINITY_DOMAIN), CreateDeviceAffinityDomain, Property::PT_STRING},
		{NIV_VALUESTRING (CL_DEVICE_PARTITION_MAX_SUB_DEVICES), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_PARTITION_PROPERTIES), CreateDevicePartitionProperty, Property::PT_STRING},
		{NIV_VALUESTRING (CL_DEVICE_PARTITION_TYPE), CreateDevicePartitionProperty, Property::PT_STRING},
		{NIV_VALUESTRING (CL_DEVICE_PREFERRED_VECTOR_WIDTH_CHAR), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_PREFERRED_VECTOR_WIDTH_SHORT), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_PREFERRED_VECTOR_WIDTH_INT), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_PREFERRED_VECTOR_WIDTH_LONG), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_PREFERRED_VECTOR_WIDTH_FLOAT), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_PREFERRED_VECTOR_WIDTH_DOUBLE), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_PREFERRED_VECTOR_WIDTH_HALF), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_PRINTF_BUFFER_SIZE), CreateSizeT, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_PROFILE), CreateChar, Property::PT_STRING},
		{NIV_VALUESTRING (CL_DEVICE_PROFILING_TIMER_RESOLUTION), CreateSizeT, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_QUEUE_PROPERTIES), CreateCommandQueueProperties, Property::PT_STRING},
		{NIV_VALUESTRING (CL_DEVICE_SINGLE_FP_CONFIG), CreateDeviceFPConfig, Property::PT_STRING},
		{NIV_VALUESTRING (CL_DEVICE_TYPE), CreateDeviceType, Property::PT_STRING},
		{NIV_VALUESTRING (CL_DEVICE_VENDOR), CreateChar, Property::PT_STRING},
		{NIV_VALUESTRING (CL_DEVICE_VENDOR_ID), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_VERSION), CreateChar, Property::PT_STRING},
		{NIV_VALUESTRING (CL_DRIVER_VERSION), CreateChar, Property::PT_STRING}
	};
#endif

#if CL_VERSION_2_0
	static const std::vector<PropertyFetcher<cl_device_info>> infos_CL_2_0 = {
		{NIV_VALUESTRING (CL_DEVICE_GLOBAL_VARIABLE_PREFERRED_TOTAL_SIZE), CreateSizeT, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_MAX_GLOBAL_VARIABLE_SIZE), CreateSizeT, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_MAX_ON_DEVICE_EVENTS), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_MAX_ON_DEVICE_QUEUES), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_MAX_PIPE_ARGS), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_MAX_READ_WRITE_IMAGE_ARGS), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_PIPE_MAX_ACTIVE_RESERVATIONS), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_PIPE_MAX_PACKET_SIZE), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_PREFERRED_GLOBAL_ATOMIC_ALIGNMENT), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_PREFERRED_INTEROP_USER_SYNC), CreateBool, Property::PT_BOOL},
		{NIV_VALUESTRING (CL_DEVICE_PREFERRED_LOCAL_ATOMIC_ALIGNMENT), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_PREFERRED_PLATFORM_ATOMIC_ALIGNMENT), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_QUEUE_ON_DEVICE_MAX_SIZE), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_QUEUE_ON_DEVICE_PREFERRED_SIZE), CreateUInt, Property::PT_INT64},
		{NIV_VALUESTRING (CL_DEVICE_QUEUE_ON_DEVICE_PROPERTIES), CreateCommandQueueProperties, Property::PT_STRING},
		{NIV_VALUESTRING (CL_DEVICE_QUEUE_ON_HOST_PROPERTIES), CreateCommandQueueProperties, Property::PT_STRING},
		{NIV_VALUESTRING (CL_DEVICE_SVM_CAPABILITIES), CreateDeviceSVMCapabilities, Property::PT_STRING}
	};

	// Extension properties
	static const std::vector<PropertyFetcher<cl_device_info>> infos_CL_2_0_Ext = {
		// {NIV_VALUESTRING (CL_DEVICE_SPIR_VERSIONS), CreateCharList, Property::PT_STRING},
		// {NIV_VALUESTRING (CL_DEVICE_TERMINATE_CAPABILITY_KHR), CreateDeviceTerminateCapability, Property::PT_STRING},
	};
#endif

	Node* node = pool.Allocate<Node> ();
	node->name = "Device";

	// Get OpenCL version
	std::size_t versionSize;
	NIV_SAFE_CL (clGetDeviceInfo(id, CL_DEVICE_VERSION, 0, nullptr, &versionSize));

	std::string versionString;
	versionString.resize (versionSize);
	NIV_SAFE_CL (clGetDeviceInfo(id, CL_DEVICE_VERSION, versionSize,
		// Ugly but safe
		const_cast<char*> (versionString.data ()), nullptr));

	const auto version = ParseVersion (versionString.c_str ());

	if (version == Version (1, 0)) {
		GetProperties (pool, node, clGetDeviceInfo, id, infos_CL_1_0);
	}
#if CL_VERSION_1_1
	else if (version == Version (1, 1)) {
		GetProperties (pool, node, clGetDeviceInfo, id, infos_CL_1_1);
	}
#endif
#if CL_VERSION_1_2
	else if (version >= Version (1, 2)) {
		GetProperties (pool, node, clGetDeviceInfo, id, infos_CL_1_2);
	}
#endif

#if CL_VERSION_2_0
	if (version >= Version (2, 0)) {
		GetProperties (pool, node, clGetDeviceInfo, id, infos_CL_2_0);
	}
#endif

	cl_int result;
	auto ctx = clCreateContext (nullptr, 1, &id, nullptr, nullptr, &result);

	if (result == CL_SUCCESS) {
		node->firstChild = GatherContextInfo (ctx, pool, version);
	}

	return node;
}
}

////////////////////////////////////////////////////////////////////////////////
Node* GatherOpenCLInfo (Pool<>& pool)
{
	Node* root = pool.Allocate <Node> ();

	root->name = "Platforms";

	cl_uint numPlatforms;

	NIV_SAFE_CL(clGetPlatformIDs (0, NULL, &numPlatforms));
	if (numPlatforms <= 0) {
		std::cerr << "Failed to find any OpenCL platform." << std::endl;
		return nullptr;
	}

	std::vector<cl_platform_id> platformIds (numPlatforms);
	NIV_SAFE_CL(clGetPlatformIDs (numPlatforms, platformIds.data (), nullptr));

	for (const auto platformId : platformIds) {
		Node* platform = pool.Allocate <Node> ();
		root->firstChild = platform;
		platform->name = "Platform";

		static const std::vector<PropertyFetcher<cl_platform_info>> infos = {
			{ NIV_VALUESTRING (CL_PLATFORM_PROFILE), CreateChar, Property::PT_STRING},
			{ NIV_VALUESTRING (CL_PLATFORM_VERSION), CreateChar, Property::PT_STRING},
			{ NIV_VALUESTRING (CL_PLATFORM_NAME), CreateChar, Property::PT_STRING},
			{ NIV_VALUESTRING (CL_PLATFORM_VENDOR), CreateChar, Property::PT_STRING},
			{ NIV_VALUESTRING (CL_PLATFORM_EXTENSIONS), CreateCharList, Property::PT_STRING}
		};

		GetProperties (pool, platform, clGetPlatformInfo, platformId, infos);

		cl_uint numDevices;
		NIV_SAFE_CL (clGetDeviceIDs (platformId, CL_DEVICE_TYPE_ALL, 0, nullptr, &numDevices));
		std::vector<cl_device_id> deviceIds (numDevices);
		NIV_SAFE_CL (clGetDeviceIDs (platformId, CL_DEVICE_TYPE_ALL, numDevices, deviceIds.data (), 0));

		Node* last = nullptr;

		for (const auto deviceId : deviceIds) {
			Node* deviceNode = GatherDeviceInfo (deviceId, pool);

			if (last) {
				last->next = deviceNode;
				last = last->next;
			} else {
				platform->next = deviceNode;
				last = deviceNode;
			}
		}
	}

	return root;
}

////////////////////////////////////////////////////////////////////////////////
int main(int argc, char* argv[])
{
	try {
		Pool<> pool;
		auto root = GatherOpenCLInfo (pool);

		if (argc == 2) {
			if (argv [1][0] == '-') {
				switch (argv [1][1]) {
				case 'x':
				{
					XmlPrinter xmlPrinter;
					xmlPrinter.Write (std::cout, root);
					break;
				}

				case 'j':
				{
					JsonPrinter jsonPrinter;
					jsonPrinter.Write (std::cout, root);
					break;
				}
				}
			}
		} else {
			XmlPrinter xmlPrinter;
			xmlPrinter.Write (std::cout, root);
		}
	} catch (...) {
		std::cerr << "Error while obtaining OpenCL diagnostic information\n";
		return 1;
	}

	return 0;
}
