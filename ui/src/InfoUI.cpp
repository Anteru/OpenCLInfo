/**
* @author: Matthaeus G. "Anteru" Chajdas
Licensed under the 3-clause BSD license
*/

#include "InfoUI.h"

#include "clInfo.h"

#include <QMessageBox>
#include <QStandardItemModel>
#include <QStandardItem>
#include <QTreeWidgetItem>

#include <cstdint>

////////////////////////////////////////////////////////////////////////////////
const char* GetPlatformName (const cliNode* platform)
{
	for (auto p = platform->firstProperty; p; p = p->next) {
		if (::strcmp (p->name, "CL_PLATFORM_NAME") == 0) {
			return p->value->s;
		}
	}

	return nullptr;
}

////////////////////////////////////////////////////////////////////////////////
const char* GetDeviceName (const cliNode* device)
{
	for (auto p = device->firstProperty; p; p = p->next) {
		if (::strcmp (p->name, "CL_DEVICE_NAME") == 0) {
			return p->value->s;
		}
	}

	return nullptr;
}

////////////////////////////////////////////////////////////////////////////////
const cliNode* GetImageFormats (const cliNode* device)
{
	for (auto n = device->firstChild; n; n = n->next) {
		if (::strcmp (n->name, "ImageFormats") == 0) {
			return n;
		}
	}

	return nullptr;
}

////////////////////////////////////////////////////////////////////////////////
InfoUI::InfoUI ()
{
	ui_.setupUi (this);

	ui_.splitterMain->setStretchFactor (0, 1);
	ui_.splitterMain->setStretchFactor (1, 3);

	ui_.splitterDevice->setStretchFactor (0, 3);
	ui_.splitterDevice->setStretchFactor (1, 1);

	connect (ui_.platformList, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
		this, &InfoUI::PlatformSelected);

	connect (ui_.deviceList, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
		this, &InfoUI::DeviceSelected);

	cliInfo_Create (&cliInfo_);
	cliInfo_Gather (cliInfo_);

	cliNode* root;
	cliInfo_GetRoot (cliInfo_, &root);

	int itemCount = 0;
	for (auto n = root->firstChild; n; n = n->next) {
		ui_.platformList->addItem (GetPlatformName (n),
			QVariant::fromValue (reinterpret_cast<std::intptr_t> (n)));
		++itemCount;
	}

	statusBar ()->showMessage (QString ("Found %1 platform(s)").arg (itemCount));

	connect (ui_.actionAbout, &QAction::triggered, [this]() -> void {
		QMessageBox::about (this, "About OpenCL info viewer",
			"<strong>OpenCL info viewer</strong><br/>(C) 2015 Matthäus G. Chajdas");
	});

	connect (ui_.actionAbout_Qt, &QAction::triggered, [this]() -> void {
		QMessageBox::aboutQt (this);
	});
}

////////////////////////////////////////////////////////////////////////////////
InfoUI::~InfoUI ()
{
	cliInfo_Destroy (cliInfo_);
}

////////////////////////////////////////////////////////////////////////////////
QTreeWidgetItem* ToTree (const cliProperty* p)
{
	auto propertyItem = new QTreeWidgetItem (QTreeWidgetItem::UserType);

	propertyItem->setText (0, p->name);

	if (p->hint) {
		propertyItem->setToolTip (0, p->hint);
	}

	for (auto v = p->value; v; v = v->next) {
		auto valueItem = new QTreeWidgetItem (propertyItem,
			QTreeWidgetItem::UserType);

		switch (p->type) {
			case CLI_PropertyType_String:
			{
				valueItem->setText (0, v->s);
				break;
			}

			case CLI_PropertyType_Int64:
			{
				valueItem->setText (0, QString::number (v->i));
				break;
			}

			case CLI_PropertyType_Bool:
			{
				valueItem->setCheckState (0, v->b ? Qt::Checked : Qt::Unchecked);
				valueItem->setFlags (Qt::ItemIsEnabled | Qt::ItemIsSelectable);
				valueItem->setText (0, v->b ? "true" : "false");
				break;
			}
		}
	}

	return propertyItem;
}

////////////////////////////////////////////////////////////////////////////////
QTreeWidgetItem* ToTree (const cliNode* node, bool onlyProperties = false)
{
	auto nodeItem = new QTreeWidgetItem (QTreeWidgetItem::UserType);

	nodeItem->setText (0, node->name);

	if (! onlyProperties) {
		for (auto n = node->firstChild; n; n = n->next) {
			nodeItem->addChild (ToTree (n, onlyProperties));
		}
	}

	for (auto p = node->firstProperty; p; p = p->next) {
		nodeItem->addChild (ToTree (p));
	}

	return nodeItem;
}

////////////////////////////////////////////////////////////////////////////////
QTreeWidgetItem* ImageFormatsToTree (const cliNode* node)
{
	auto imageFormatsItem = new QTreeWidgetItem (QTreeWidgetItem::UserType);

	for (auto imageType = node->firstChild; imageType; imageType = imageType->next) {
		auto imageTypeItem = new QTreeWidgetItem (QTreeWidgetItem::UserType);
		imageTypeItem->setText (0, imageType->kind);

		// We group by data type
		QMap<QString, QStringList> formats;

		for (auto n = imageType->firstChild; n; n = n->next) {
			// All children are format here, with two properties:
			// ChannelOrder and ChannelDataType

			QString channelOrder, channelDataType;

			for (auto p = n->firstProperty; p; p = p->next) {
				if (::strcmp (p->name, "ChannelOrder") == 0) {
					channelOrder = p->value->s;
				} else if (::strcmp (p->name, "ChannelDataType") == 0) {
					channelDataType = p->value->s;
				}
			}

			formats [channelDataType].append (channelOrder);
		}

		for (auto it = formats.begin (); it != formats.end (); ++it) {
			auto formatItem = new QTreeWidgetItem (QTreeWidgetItem::UserType);
			formatItem->setText (0, it.key ());

			for (const auto& s : it.value ()) {
				auto channelItem = new QTreeWidgetItem (QTreeWidgetItem::UserType);
				channelItem->setText (0, s);
				formatItem->addChild (channelItem);
			}

			imageTypeItem->addChild (formatItem);
		}

		imageFormatsItem->addChild (imageTypeItem);
	}

	return imageFormatsItem;
}

////////////////////////////////////////////////////////////////////////////////
void InfoUI::PlatformSelected (const int index)
{
	ui_.platformInfo->clear ();

	if (index == -1) {
		return;
	}

	auto platformNode = reinterpret_cast<const cliNode*> (
				ui_.platformList->itemData (index).value<std::intptr_t> ());

	auto properties = ToTree (platformNode, true);
	ui_.platformInfo->addTopLevelItems (properties->takeChildren ());
	delete properties;

	// Iterate devices and populate our combobox
	ui_.deviceList->clear ();
	for (auto n = platformNode->firstChild; n; n = n->next) {
		if (::strcmp (n->name, "Devices") == 0) {
			for (auto d = n->firstChild; d; d = d->next) {
				ui_.deviceList->addItem (GetDeviceName (d),
					QVariant::fromValue (reinterpret_cast<std::intptr_t> (d)));
			}
		}
	}
}

////////////////////////////////////////////////////////////////////////////////
void InfoUI::DeviceSelected (const int index)
{
	ui_.deviceInfo->clear ();
	ui_.imageFormats->clear ();

	if (index == -1) {
		return;
	}

	auto deviceInfo = ToTree (reinterpret_cast<const cliNode*> (
		ui_.deviceList->itemData (index).value<std::intptr_t> ()), true);
	ui_.deviceInfo->addTopLevelItems (deviceInfo->takeChildren ());
	delete deviceInfo;

	auto imageFormats = ImageFormatsToTree (
		GetImageFormats (reinterpret_cast<const cliNode*> (
			ui_.deviceList->itemData (index).value<std::intptr_t> ())));
	ui_.imageFormats->addTopLevelItems (imageFormats->takeChildren ());
	delete imageFormats;
}
