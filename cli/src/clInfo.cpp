// Matthäus G. Chajdas
// Licensed under the 3-clause BSD license

#include <clInfo.h>

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

namespace {
/**
Dump tree to XML.

The output is unformatted, that is, there is no whitespace between elements.
*/
struct XmlPrinter
{
public:
	void Write (std::ostream& s, const cliNode* tree) const
	{
		OnNode (s, tree);
	}

private:
	void OnNode (std::ostream& s, const cliNode* node) const
	{
		s << "<" << node->name;

		if (node->kind) {
			s << " Kind=\"" << node->kind << "\"";
		}
		s << ">";

		for (auto p = node->firstProperty; p; p = p->next) {
			OnProperty (s, p);
		}

		for (auto n = node->firstChild; n; n = n->next) {
			OnNode (s, n);
		}

		s << "</" << node->name << ">";
	}

	void OnProperty (std::ostream& s, const cliProperty* p) const
	{
		const char* t = nullptr;
		switch (p->type) {
		case CLI_PropertyType_Bool: t = "bool"; break;
		case CLI_PropertyType_Int64: t = "int64"; break;
		case CLI_PropertyType_String: t = "string"; break;
		}

		s << "<Property Name=\"" << p->name << "\" Type=\"" << t << "\">";

		for (auto v = p->value; v; v = v->next) {
			s << "<Value>";

			switch (p->type) {
			case CLI_PropertyType_Bool:
				if (v->b) {
					s << "true";
				} else {
					s << "false";
				}
				break;


			case CLI_PropertyType_Int64:
				s << v->i;
				break;

			case CLI_PropertyType_String:
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
	void Write (std::ostream& s, const cliNode* tree) const
	{
		OnNode (s, tree);
	}

private:
	void OnNode (std::ostream& s, const cliNode* node) const
	{
		s << "{ \"" << node->name << "\" : {";
		s << "\"Properties\" : ";

		if (node->firstProperty) {
			for (auto p = node->firstProperty; p; p = p->next) {
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
			for (auto n = node->firstChild; n; n = n->next) {
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

	void OnProperty (std::ostream& s, const cliProperty* p) const
	{
		bool singleValue = (p->value && p->value->next == nullptr);

		if (!singleValue) {
			s << "\"" << p->name << "\" = [";
		} else {
			s << "\"" << p->name << "\" = ";
		}

		for (auto v = p->value; v; v = v->next) {
			switch (p->type) {
			case CLI_PropertyType_Bool:
				if (v->b) {
					s << "true";
				} else {
					s << "false";
				}
				break;


			case CLI_PropertyType_Int64:
				s << v->i;
				break;

			case CLI_PropertyType_String:
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

/**
Dump tree, formatted for reading on a console.

The output is pretty-printed for consoles
*/
struct ConsolePrinter
{
public:
	void Write (std::ostream& s, const cliNode* tree) const
	{
		OnNode (s, tree, 0);
	}

private:
	static void Indent (std::ostream& s, const int indentation)
	{
		for (int i = 0; i < indentation; ++i) {
			s << "  ";
		}
	}

	void OnNode (std::ostream& s, const cliNode* node, const int indentation) const
	{
		Indent (s, indentation);
		s << node->name << '\n';

		std::size_t maxPropertyLength = 0;
		for (auto p = node->firstProperty; p; p = p->next) {
			maxPropertyLength = std::max (maxPropertyLength,
				::strlen (p->name));
		}

		for (auto p = node->firstProperty; p; p = p->next) {
			OnProperty (s, p, maxPropertyLength, indentation + 1);
		}

		for (auto n = node->firstChild; n; n = n->next) {
			OnNode (s, n, indentation + 1);
			s << '\n';
		}
	}

	void OnProperty (std::ostream& s, const cliProperty* p,
		const std::size_t fieldWidth, const int indentation) const
	{
		Indent (s, indentation);

		s << std::left << std::setw (fieldWidth) << p->name << " : ";

		for (auto v = p->value; v; v = v->next) {
			switch (p->type) {
			case CLI_PropertyType_Bool:
				if (v->b) {
					s << "true";
				} else {
					s << "false";
				}
				break;


			case CLI_PropertyType_Int64:
				s << v->i;
				break;

			case CLI_PropertyType_String:
				s << v->s;
				break;
			}

			if (v->next) {
				s << ' ';
			}
		}

		s << '\n';
	}
};
}

////////////////////////////////////////////////////////////////////////////////
int main(int argc, char* argv[])
{
	try {
		struct cliInfo* info;
		cliInfo_Create (&info);
		cliInfo_Gather (info);

		struct cliNode* root;
		cliInfo_GetRoot (info, &root);

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

				case 'c':
				{
					ConsolePrinter consolePrinter;
					consolePrinter.Write (std::cout, root);
					break;
				}
				}
			}
		} else {
			ConsolePrinter consolePrinter;
			consolePrinter.Write (std::cout, root);
		}

		cliInfo_Destroy (info);
	} catch (...) {
		std::cerr << "Error while obtaining OpenCL diagnostic information\n";
		return 1;
	}

	return 0;
}
