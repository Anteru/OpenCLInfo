/**
* @author: Matthaeus G. "Anteru" Chajdas
Licensed under the 3-clause BSD license
*/

#ifndef NIV_NIVEN_TOOLS_OPENCLINFOVIEWER_H_AD0BEF12F844D07A15146B511E0436B49E51ABB1
#define NIV_NIVEN_TOOLS_OPENCLINFOVIEWER_H_AD0BEF12F844D07A15146B511E0436B49E51ABB1

#include "ui_InfoUI.h"

struct cliInfo;
struct cliNode;

class InfoUI final : public QMainWindow
{
	Q_OBJECT

public:
	InfoUI ();
	~InfoUI ();

public slots:
	void PlatformSelected (const int index);
	void DeviceSelected (const int index);

private:
	Ui::InfoUI ui_;

	cliInfo*	cliInfo_ = nullptr;
};
#endif

