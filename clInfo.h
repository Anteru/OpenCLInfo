/**
* @author: Matthaeus G. "Anteru" Chajdas
*
* License: NDL
*/

#ifndef NIV_CLINFO_H_293E9B16E02AFDE6E65A7A5640D52027F79EC4AF
#define NIV_CLINFO_H_293E9B16E02AFDE6E65A7A5640D52027F79EC4AF

#include <stdbool.h>
#include <stdint.h>

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
A property. May contain one value, in this case, value will be non-zero.
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
*/
struct cliNode
{
	/* For example, name = ObjectType, kind = Image1D,
	   in general, kind will be a nullptr.
	*/
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

#ifdef __cplusplus
extern "C" {
#endif
/*
These functions return CLDS_SUCCESS if everything worked fine.
*/

int cliInfo_Create (struct cliInfo** info);
int cliInfo_Gather (struct cliInfo* info);
int cliInfo_GetRoot (const struct cliInfo* info, struct cliNode** root);
int cliInfo_Destroy (struct cliInfo* info);

#ifdef __cplusplus
}
#endif
#endif

