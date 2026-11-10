// Downloadable content (Xbox Live Marketplace packages). See dlc_packages.cpp.

#pragma once

// Installs the content packages waiting in dlc/ next to the executable (the data folder on
// Android, where the launcher copies the packages the player picks) for the running title.
// Called once the title's module is loaded and before its main thread starts.
void InstallDlcPackages();
