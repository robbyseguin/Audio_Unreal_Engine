/*
  ==============================================================================

   This file is part of the JUCE framework.
   Copyright (c) Raw Material Software Limited

   JUCE is an open source framework subject to commercial or open source
   licensing.

   By downloading, installing, or using the JUCE framework, or combining the
   JUCE framework with any other source code, object code, content or any other
   copyrightable work, you agree to the terms of the JUCE End User Licence
   Agreement, and all incorporated terms including the JUCE Privacy Policy and
   the JUCE Website Terms of Service, as applicable, which will bind you. If you
   do not agree to the terms of these agreements, we will not license the JUCE
   framework to you, and you must discontinue the installation or download
   process and cease use of the JUCE framework.

   JUCE End User Licence Agreement: https://juce.com/legal/juce-8-licence/
   JUCE Privacy Policy: https://juce.com/juce-privacy-policy
   JUCE Website Terms of Service: https://juce.com/juce-website-terms-of-service/

   Or:

   You may also use this code under the terms of the AGPLv3:
   https://www.gnu.org/licenses/agpl-3.0.en.html

   THE JUCE FRAMEWORK IS PROVIDED "AS IS" WITHOUT ANY WARRANTY, AND ALL
   WARRANTIES, WHETHER EXPRESSED OR IMPLIED, INCLUDING WARRANTY OF
   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, ARE DISCLAIMED.

  ==============================================================================
*/

namespace juce
{

#define JNI_CLASS_MEMBERS(METHOD, STATICMETHOD, FIELD, STATICFIELD, CALLBACK) \
  METHOD (getItemCount, "getItemCount", "()I") \
  METHOD (getItemAt,    "getItemAt",    "(I)Landroid/content/ClipData$Item;")

DECLARE_JNI_CLASS (ClipData, "android/content/ClipData")
#undef JNI_CLASS_MEMBERS

#define JNI_CLASS_MEMBERS(METHOD, STATICMETHOD, FIELD, STATICFIELD, CALLBACK) \
  METHOD (getUri, "getUri", "()Landroid/net/Uri;")

DECLARE_JNI_CLASS (ClipDataItem, "android/content/ClipData$Item")
#undef JNI_CLASS_MEMBERS

class FileChooser::Native final : public FileChooser::Pimpl
{
public:
    //==============================================================================
    Native (FileChooser& fileChooser, int flags)    : owner (fileChooser)
    {
        if (currentFileChooser == nullptr)
        {
            currentFileChooser = this;
            auto* env = getEnv();

            auto saveMode           = ((flags & FileBrowserComponent::saveMode) != 0);
            auto selectsDirectories = ((flags & FileBrowserComponent::canSelectDirectories) != 0);
            auto canSelectMultiple  = ((flags & FileBrowserComponent::canSelectMultipleItems) != 0);

            // You cannot save a directory
            jassert (! (saveMode && selectsDirectories));

            const char* action = (selectsDirectories ? "android.intent.action.OPEN_DOCUMENT_TREE"
                                                     : (saveMode ? "android.intent.action.CREATE_DOCUMENT"
                                                                 : "android.intent.action.OPEN_DOCUMENT"));


            intent = GlobalRef (LocalRef<jobject> (env->NewObject (AndroidIntent, AndroidIntent.constructWithString,
                                                                   javaString (action).get())));

            if (owner.startingFile != File())
            {
                if (saveMode && (! owner.startingFile.isDirectory()))
                    env->CallObjectMethod (intent.get(), AndroidIntent.putExtraString,
                                           javaString ("android.intent.extra.TITLE").get(),
                                           javaString (owner.startingFile.getFileName()).get());


                URL url (owner.startingFile);
                LocalRef<jobject> uri (env->CallStaticObjectMethod (AndroidUri, AndroidUri.parse,
                                                                    javaString (url.toString (true)).get()));

                if (uri)
                    env->CallObjectMethod (intent.get(), AndroidIntent.putExtraParcelable,
                                           javaString ("android.provider.extra.INITIAL_URI").get(),
                                           uri.get());
            }

            env->CallObjectMethod (intent.get(),
                                   AndroidIntent.putExtraBool,
                                   javaString ("android.intent.extra.ALLOW_MULTIPLE").get(),
                                   canSelectMultiple);

            if (! selectsDirectories)
            {
                env->CallObjectMethod (intent.get(), AndroidIntent.addCategory,
                                       javaString ("android.intent.category.OPENABLE").get());

                auto mimeTypes = convertFiltersToMimeTypes (owner.filters);

                if (mimeTypes.size() == 1)
                {
                    env->CallObjectMethod (intent.get(), AndroidIntent.setType, javaString (mimeTypes[0]).get());
                }
                else
                {
                    String mimeGroup = "*";

                    if (mimeTypes.size() > 0)
                    {
                        mimeGroup = mimeTypes[0].upToFirstOccurrenceOf ("/", false, false);
                        auto allMimeTypesHaveSameGroup = true;

                        LocalRef<jobjectArray> jMimeTypes (env->NewObjectArray (mimeTypes.size(), JavaString,
                                                                                javaString ("").get()));

                        for (int i = 0; i < mimeTypes.size(); ++i)
                        {
                            env->SetObjectArrayElement (jMimeTypes.get(), i, javaString (mimeTypes[i]).get());

                            if (mimeGroup != mimeTypes[i].upToFirstOccurrenceOf ("/", false, false))
                                allMimeTypesHaveSameGroup = false;
                        }

                        env->CallObjectMethod (intent.get(), AndroidIntent.putExtraStrings,
                                               javaString ("android.intent.extra.MIME_TYPES").get(),
                                               jMimeTypes.get());

                        if (! allMimeTypesHaveSameGroup)
                            mimeGroup = "*";
                    }

                    env->CallObjectMethod (intent.get(), AndroidIntent.setType, javaString (mimeGroup + "/*").get());
                }
            }
        }
        else
            jassertfalse; // there can only be a single file chooser
    }

    ~Native() override
    {
        masterReference.clear();
        currentFileChooser = nullptr;
    }

    void runModally() override
    {
        // Android does not support modal file choosers
        jassertfalse;
    }

    void launch() override
    {
        auto* env = getEnv();

        if (currentFileChooser != nullptr)
        {
            startAndroidActivityForResult (LocalRef<jobject> (env->NewLocalRef (intent.get())), /*READ_REQUEST_CODE*/ 42,
                                           [myself = WeakReference<Native> { this }] (int requestCode, int resultCode, LocalRef<jobject> intentData) mutable
                                           {
                                               if (myself != nullptr)
                                                   myself->onActivityResult (requestCode, resultCode, intentData);
                                           });
        }
        else
        {
            jassertfalse; // There is already a file chooser running
        }
    }

    void onActivityResult (int /*requestCode*/, int resultCode, const LocalRef<jobject>& intentData)
    {
        currentFileChooser = nullptr;
        auto* env = getEnv();

        const auto getUrls = [&]() -> Array<URL>
        {
            if (resultCode != /*Activity.RESULT_OK*/ -1 || intentData == nullptr)
                return {};

            Array<URL> chosenURLs;

            const auto addUrl = [env, &chosenURLs] (jobject uri)
            {
                if (auto jStr = (jstring) env->CallObjectMethod (uri, JavaObject.toString))
                    chosenURLs.add (URL (juceString (env, jStr)));
            };

            if (LocalRef<jobject> clipData { env->CallObjectMethod (intentData.get(), AndroidIntent.getClipData) })
            {
                const auto count = env->CallIntMethod (clipData.get(), ClipData.getItemCount);

                for (auto i = 0; i < count; ++i)
                {
                    if (LocalRef<jobject> item { env->CallObjectMethod (clipData.get(), ClipData.getItemAt, i) })
                    {
                        if (LocalRef<jobject> itemUri { env->CallObjectMethod (item.get(), ClipDataItem.getUri) })
                            addUrl (itemUri.get());
                    }
                }
            }
            else if (LocalRef<jobject> uri { env->CallObjectMethod (intentData.get(), AndroidIntent.getData )})
            {
                addUrl (uri.get());
            }

            return chosenURLs;
        };

        owner.finished (getUrls());
    }

    static Native* currentFileChooser;

    static StringArray convertFiltersToMimeTypes (const String& fileFilters)
    {
        StringArray result;
        auto wildcards = StringArray::fromTokens (fileFilters, ";", "");

        for (auto wildcard : wildcards)
        {
            if (wildcard.upToLastOccurrenceOf (".", false, false) == "*")
            {
                auto extension = wildcard.fromLastOccurrenceOf (".", false, false);

                result.addArray (detail::MimeTypeTable::getMimeTypesForFileExtension (extension));
            }
        }

        result.removeDuplicates (false);
        return result;
    }

private:
    JUCE_DECLARE_WEAK_REFERENCEABLE (Native)

    FileChooser& owner;
    GlobalRef intent;
};

FileChooser::Native* FileChooser::Native::currentFileChooser = nullptr;

std::shared_ptr<FileChooser::Pimpl> FileChooser::showPlatformDialog (FileChooser& owner, int flags,
                                                                     FilePreviewComponent*)
{
    if (FileChooser::Native::currentFileChooser == nullptr)
        return std::make_shared<FileChooser::Native> (owner, flags);

    // there can only be one file chooser on Android at a once
    jassertfalse;
    return nullptr;
}

bool FileChooser::isPlatformDialogAvailable()
{
   #if JUCE_DISABLE_NATIVE_FILECHOOSERS
    return false;
   #else
    return true;
   #endif
}

void FileChooser::registerCustomMimeTypeForFileExtension (const String& mimeType,
                                                          const String& fileExtension)
{
    detail::MimeTypeTable::registerCustomMimeTypeForFileExtension (mimeType, fileExtension);
}

} // namespace juce
