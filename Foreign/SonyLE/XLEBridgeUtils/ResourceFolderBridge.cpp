// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../../Tools/GUILayer/MarshalString.h"
#include "../../Tools/GUILayer/CLIXAutoPtr.h"
#include "../../../Assets/IFileSystem.h"
#include "../../../ConsoleRig/Log.h"
#include "../../../Utility/Streams/PathUtils.h"
#include "../../../Utility/Conversion.h"

using namespace System;
using namespace System::Collections::Generic;

#pragma warning(disable:4505) // 'XLEBridgeUtils::Marshal': unreferenced local function has been removed)

#undef new

namespace XLEBridgeUtils
{
	public ref class ResourceFolderBridge : public LevelEditorCore::IOpaqueResourceFolder
	{
	public:
		property IEnumerable<LevelEditorCore::IOpaqueResourceFolder^>^ Subfolders { 
			virtual IEnumerable<LevelEditorCore::IOpaqueResourceFolder^>^ get();
		}
		property bool IsLeaf { 
			virtual bool get();
		}
        property IEnumerable<Object^>^ Resources { 
			virtual IEnumerable<Object^>^ get();
		}
        property LevelEditorCore::IOpaqueResourceFolder^ Parent { 
			virtual LevelEditorCore::IOpaqueResourceFolder^ get() { return nullptr; }
		}
        property String^ Name {
			virtual String^ get() { return _name; }
		}

        // virtual Sce::Atf::IOpaqueResourceFolder^ CreateFolder();

		static ResourceFolderBridge^ BeginFromRoot();

		ResourceFolderBridge();
		ResourceFolderBridge(::Assets::FileSystemWalker&& walker, String^ name);
		~ResourceFolderBridge();
	private:
		clix::shared_ptr<::Assets::FileSystemWalker> _walker;
		String^ _name;
	};

	static String^ Marshal(const std::basic_string<utf8>& str)
	{
		return clix::detail::StringMarshaler<clix::detail::NetFromCxx>::marshalCxxString<clix::E_UTF8>(AsPointer(str.begin()), AsPointer(str.end()));
	}

	static String^ Marshal(StringSection<utf8> str)
	{
		return clix::detail::StringMarshaler<clix::detail::NetFromCxx>::marshalCxxString<clix::E_UTF8>(AsPointer(str.begin()), AsPointer(str.end()));
	}

	/*ref class FolderEnumerable : IEnumerable<LevelEditorCore::IOpaqueResourceFolder^> 
	{
	public:
		ref struct MyRangeIterator : IEnumerator<LevelEditorCore::IOpaqueResourceFolder^> 
		{
		private:
			::Assets::FileSystemWalker::DirectoryIterator _begin;
			::Assets::FileSystemWalker::DirectoryIterator _end;

			property LevelEditorCore::IOpaqueResourceFolder^ Current { 
				virtual LevelEditorCore::IOpaqueResourceFolder^ get(); // { return i; } 
			}

			bool MoveNext() { return i++ != 10; }

			property Object^ System::Collections::IEnumerator::Current2
			{
				virtual Object^ get() new sealed = System::Collections::IEnumerator::Current::get
				{ 
					return Current; 
				}
			}

			~MyRangeIterator();
			void Reset() { throw gcnew NotImplementedException(); }
		};

		virtual IEnumerator<LevelEditorCore::IOpaqueResourceFolder^>^ GetEnumerator() { return gcnew MyRangeIterator(); }

		virtual System::Collections::IEnumerator^ GetEnumerator2() new sealed = System::Collections::IEnumerable::GetEnumerator
		{
			return GetEnumerator(); 
		}
	}*/

	IEnumerable<LevelEditorCore::IOpaqueResourceFolder^>^ ResourceFolderBridge::Subfolders::get()
	{
		auto result = gcnew List<LevelEditorCore::IOpaqueResourceFolder^>();
		for (auto i=_walker->begin_directories(); i!=_walker->end_directories(); ++i)
			result->Add(gcnew ResourceFolderBridge(*i, Marshal(i.Name())));
		return result;
	}

	bool ResourceFolderBridge::IsLeaf::get()
	{
		return _walker->begin_directories() == _walker->end_directories();
	}

	IEnumerable<Object^>^ ResourceFolderBridge::Resources::get()
	{
		auto result = gcnew List<Object^>();
		for (auto i=_walker->begin_files(); i!=_walker->end_files(); ++i) {
			// auto splitter = MakeFileNameSplitter(i.Desc()._naturalName);
			// result->Add(gcnew Uri(Marshal(splitter.FileAndExtension())));
			Uri^ newUri = nullptr;
			auto marshaledName = Marshal(i.Desc()._naturalName);
			if (Uri::TryCreate(marshaledName, UriKind::Relative, newUri)) {
				result->Add(newUri);
			} else {
				Log(Warning) << "Could not create URI for the string: " << Conversion::Convert<std::string>(i.Desc()._naturalName) << std::endl;
			}
		}
		return result;
	}

	/*Sce::Atf::IOpaqueResourceFolder^ ResourceFolderBridge::CreateFolder()
	{
		throw gcnew System::Exception("Cannot create a new folder via mounted filesystem bridge");
	}*/

	ResourceFolderBridge^ ResourceFolderBridge::BeginFromRoot()
	{
		return gcnew ResourceFolderBridge(::Assets::MainFileSystem::BeginWalk(), "<root>");
	}

	ResourceFolderBridge::ResourceFolderBridge()
	{
	}

	ResourceFolderBridge::ResourceFolderBridge(::Assets::FileSystemWalker&& walker, String^ name)
	: _name(name)
	{
		_walker = std::make_shared<::Assets::FileSystemWalker>(std::move(walker));
	}

	ResourceFolderBridge::~ResourceFolderBridge() 
	{
		delete _walker;
	}


}