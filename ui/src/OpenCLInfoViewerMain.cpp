#include <QApplication>
#include <QStyleFactory>
#include <QFile>

#include "InfoUI.h"

#include <iostream>

// Licensed under the 3-clause BSD license

int main (int argc, char* argv[])
{
	try {
		QApplication app (argc, argv);
		QApplication::setApplicationName ("niven OpenCL info viewer");
		
		InfoUI infoUI;
		infoUI.show ();
		return app.exec ();
	} catch (const std::exception& e) {
		std::cout << e.what () << std::endl;
	}
}
