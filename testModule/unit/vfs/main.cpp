#include "support/expect.h"

#include "resourceModule/fileSystem/fileSystem.h"

#include <string>

namespace {
using IKIGAI::RESOURCES::FileMode;
using IKIGAI::RESOURCES::FileSystem;
using IKIGAI::TEST::expect;
using IKIGAI::TEST::require;

void write(FileSystem& fs, const std::string& path, const std::string& contents) {
	const auto file = fs.getFile(path, FileMode::WRITE);
	require(file && file->isValid(), "vfs.fixture.opensWritableFile");
	file->write(contents);
	file->close();
}

void checkMounts() {
	FileSystem fs;
	fs.addMemoryFileSystem("/");
	write(fs, "/maps/base.txt", "base");
	const auto originalGeneration = fs.generation();
	fs.addMemoryFileSystem("maps\\./");
	const auto mounts = fs.mounts();
	require(mounts.size() == 2 && mounts.back().alias == "/maps", "vfs.mounts.normalizeAlias");
	const auto nestedMount = mounts.back().id;
	expect(fs.generation() != originalGeneration && !fs.lockGeneration(originalGeneration), "vfs.mounts.invalidateOlderGeneration");
	write(fs, "/maps/local.txt", "nested");
	write(fs, "/mapsExtra/other.txt", "outside");
	const auto retained = fs.getFile("maps\\./local.txt");
	const auto fallback = fs.getFile("/maps/base.txt");
	require(retained && retained->isValid() && fallback && fallback->isValid(), "vfs.mounts.resolvesNestedAndFallbackFiles");
	expect(fallback->readStr() == "base", "vfs.mounts.fallsBackToParentAlias");

	const auto activeGeneration = fs.generation();
	require(fs.unmount(nestedMount), "vfs.mounts.unmountsById");
	expect(!fs.isFileExist("/maps/local.txt") && fs.isFileExist("/maps/base.txt") && fs.isFileExist("/mapsExtra/other.txt"),
		"vfs.mounts.unmountAffectsOnlyNestedContents");
	expect(retained->readStr() == "nested", "vfs.mounts.openFileSurvivesUnmount");
	expect(fs.generation() != activeGeneration && !fs.lockGeneration(activeGeneration), "vfs.mounts.unmountInvalidatesGeneration");
}
}

int main(int argc, char** argv) {
	return IKIGAI::TEST::runCases(argc, argv, { { "mount_resolution", checkMounts } });
}
