/**
@author: Matthaeus G. "Anteru" Chajdas
Licensed under the 3-clause BSD
*/

#ifndef NIV_CLINFO_H_293E9B16E02AFDE6E65A7A5640D52027F79EC4AF
#define NIV_CLINFO_H_293E9B16E02AFDE6E65A7A5640D52027F79EC4AF

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
Holds a property value. If next is not null, there are more values for this
property.
*/
struct cliValue
{
	union {
		int64_t		i;
		bool		b;
		const char*	s;
	};

	struct cliValue*	next;
};

enum cliPropertyType
{
	CLI_PropertyType_Int64,
	CLI_PropertyType_Bool,
	CLI_PropertyType_String
};

/**
A property with an optional value.

Hint is an optional UI display hint which explains what this property is.
*/
struct cliProperty
{
	struct cliValue*	value;

	const char*			name;
	const char*			hint;
	struct cliProperty*	next;
	cliPropertyType		type;
};

/*
An interior node in the property tree. If firstChild is non-null, there is a
child branch. Sibling nodes can be enumerated using the next pointer. Properties
are present if firstProperty is not null.

Name is a generic name like "Image", etc. If there are sub-types, kind will be
set.
*/
struct cliNode
{
	const char* name;
	const char*	kind;

	struct cliNode*		firstChild;
	struct cliNode*		next;
	struct cliProperty*	firstProperty;
};

enum cliStatus
{
	CLI_Success,
	CLI_Error
};

struct cliInfo;
/*
These functions return CLI_Success if everything worked fine.
*/

/**
Create a new cliInfo object.

The object will be empty, use cliInfo_Gather to fetch the information. The
object must be released with cliInfo_Destroy.
*/
int cliInfo_Create (struct cliInfo** info);

/**
Gather the OpenCL information.

This will populate the cliInfo object. This function must be called only once.
After calling Gather(), GetRoot() can be used to obtain the root of the data
tree.
*/
int cliInfo_Gather (struct cliInfo* info);

/**
Get the root node. The root is a 'Platforms' node, with one 'Platform' node
for each discovered platform. A platform node contains properties describing
the platform, and a 'Devices' node which contains a list of 'Device' nodes,
describing each device.
*/
int cliInfo_GetRoot (const struct cliInfo* info, struct cliNode** root);

/**
Release a cliInfo object.

This destroys the underlying tree as well; that is, all nodes/properties
obtained from this info object will be invalid afterwards.
*/
int cliInfo_Destroy (struct cliInfo* info);

#ifdef __cplusplus
}
#endif
#endif
