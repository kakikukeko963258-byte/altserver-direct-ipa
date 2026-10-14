#include "InstalledApp.h"
#include "ServerError.hpp"

#include <cstdlib>
#include <memory>

static std::string stringValue(plist_t node)
{
	char* value = NULL;
	plist_get_string_val(node, &value);
	std::unique_ptr<char, decltype(&std::free)> owned(value, &std::free);
	if (!owned)
	{
		throw ServerError(ServerErrorCode::InvalidApp);
	}
	return std::string(owned.get());
}

InstalledApp::InstalledApp(plist_t plist)
{
	auto nameNode = plist_dict_get_item(plist, "CFBundleName");
	auto identifierNode = plist_dict_get_item(plist, "CFBundleIdentifier");
	auto executableNode = plist_dict_get_item(plist, "CFBundleExecutable");

	if (nameNode == NULL || identifierNode == NULL || executableNode == NULL)
	{
		throw ServerError(ServerErrorCode::InvalidApp);
	}

	this->_name = stringValue(nameNode);
	this->_bundleIdentifier = stringValue(identifierNode);
	this->_executableName = stringValue(executableNode);
}

InstalledApp::~InstalledApp()
{
}

bool InstalledApp::operator<(const InstalledApp& app) const
{
	if (this->name() == app.name())
	{
		return this->bundleIdentifier() < app.bundleIdentifier();
	}
	else
	{
		return this->name() < app.name();
	}
}

std::string InstalledApp::name() const
{
	return _name;
}

std::string InstalledApp::bundleIdentifier() const
{
	return _bundleIdentifier;
}

std::string InstalledApp::executableName() const
{
	return _executableName;
}
