/*
 * Copyright (C) 2021 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "media_scanner.h"

namespace OHOS {
namespace Media {
using namespace std;
using namespace OHOS::AppExecFwk;

MediaScanner::MediaScanner()
{
    if (mediaScannerDb_ == nullptr) {
        mediaScannerDb_ = MediaScannerDb::GetDatabaseInstance();
    }
    isScannerInitDone_ = false;
    scanExector_.SetCallbackFunction(ScanQueueCB);
}

MediaScanner::~MediaScanner()
{
    mediaScannerDb_ = nullptr;
    if (rdbhelper_ != nullptr) {
        rdbhelper_->Release();
        rdbhelper_ = nullptr;
    }
}

// Get Media Scanner Instance
MediaScanner *MediaScanner::GetMediaScannerInstance()
{
    static MediaScanner scanner;
    return &scanner;
}

void MediaScanner::ScanQueueCB(ScanRequest scanReq)
{
    int32_t errCode = ERR_SCAN_NOT_INIT;

    MediaScanner *scanner = MediaScanner::GetMediaScannerInstance();
    if (scanner != nullptr) {
        if (!scanner->isScannerInitDone_) {
            if (scanner->InitScanner(scanner->abilityContext_)) {
                scanner->isScannerInitDone_ = true;
            }
        }

        string fileUri("");
        string path = scanReq.GetPath();
        if (scanReq.GetIsDirectory()) {
            errCode = scanner->ScanDirInternal(const_cast<string &>(path));
        } else {
            errCode = scanner->ScanFileInternal(const_cast<string &>(path));
            fileUri = scanner->mediaUri_;
        }

        scanner->ExecuteScannerClientCallback(scanReq.GetRequestId(), errCode, fileUri, path);
    }
}

bool MediaScanner::InitScanner(const std::shared_ptr<Context> &context)
{
    auto contextUri = make_unique<Uri>(MEDIALIBRARY_DATA_URI);
    if ((context != nullptr) && (contextUri != nullptr)) {
        rdbhelper_ = OHOS::AppExecFwk::DataAbilityHelper::Creator(context, move(contextUri));
        if (rdbhelper_ != nullptr) {
            mediaScannerDb_->SetRdbHelper(rdbhelper_);
            return true;
        }
    }

    return false;
}

vector<string> MediaScanner::GetSupportedMimeTypes()
{
    vector<string> mimeTypeList;
    mimeTypeList.push_back(DEFAULT_AUDIO_MIME_TYPE);
    mimeTypeList.push_back(DEFAULT_VIDEO_MIME_TYPE);
    mimeTypeList.push_back(DEFAULT_IMAGE_MIME_TYPE);
    mimeTypeList.push_back(DEFAULT_FILE_MIME_TYPE);

    return mimeTypeList;
}

int32_t MediaScanner::ScanFile(string &path, const sptr<IRemoteObject> &remoteCallback)
{
    int32_t errCode = ERR_MEM_ALLOC_FAIL;
    bool isDir = false;

    if (path.empty()) {
        MEDIA_ERR_LOG("Scanfile: Path is empty");
        return ERR_EMPTY_ARGS;
    }

    if (ScannerUtils::GetAbsolutePath(path) != ERR_SUCCESS) {
        MEDIA_ERR_LOG("Scanfile: Incorrect path or insufficient permission %{public}s", path.c_str());

        unique_ptr<Metadata> metaData = mediaScannerDb_->ReadMetadata(path);
        if (metaData != nullptr && !metaData->GetFilePath().empty()) {
            vector<string> idList = {to_string(metaData->GetFileId())};
            if (mediaScannerDb_->DeleteMetadata(idList)) {
                mediaScannerDb_->NotifyDatabaseChange(metaData->GetFileMediaType());
            }
        }
        return ERR_INCORRECT_PATH;
    }

    if (ScannerUtils::IsDirectory(path)) {
        MEDIA_ERR_LOG("ScanFile: Incorrect path %{public}s", path.c_str());
        return ERR_INCORRECT_PATH;
    }

    unique_ptr<ScanRequest> scanReq = make_unique<ScanRequest>(path);
    if (scanReq != nullptr) {
        scanReq->SetIsDirectory(isDir);

        int32_t reqId = GetAvailableRequestId();
        scanReq->SetRequestId(reqId);

        scanExector_.ExecuteScan(move(scanReq));

        // After path validation and queue_ addition is success, add the callback object to callback map
        auto callback = iface_cast<IMediaScannerOperationCallback>(remoteCallback);
        if (callback != nullptr) {
            StoreCallbackObjInMap(reqId, callback);
            errCode = ERR_SUCCESS;
        }
    }

    return errCode;
}

int32_t MediaScanner::ScanDir(string &path, const sptr<IRemoteObject> &remoteCallback)
{
    int32_t errCode = ERR_MEM_ALLOC_FAIL;
    bool isDir = true;

    if (path.empty()) {
        MEDIA_ERR_LOG("ScanDir: Path is empty");
        return ERR_EMPTY_ARGS;
    }

    // Get Absolute path
    if (ScannerUtils::GetAbsolutePath(path) != ERR_SUCCESS) {
        MEDIA_ERR_LOG("ScanDir: Incorrect path or insufficient permission %{public}s", path.c_str());
        // If the path is not available, clear the same from database too if present
        CleanupDirectory(path);
        return ERR_INCORRECT_PATH;
    }

    if (!ScannerUtils::IsDirectory(path)) {
        MEDIA_ERR_LOG("ScanDir: Path provided is not a directory %{public}s", path.c_str());
        return ERR_INCORRECT_PATH;
    }

    std::unique_ptr<ScanRequest> scanReq = std::make_unique<ScanRequest>(path);
    if (scanReq != nullptr) {
        scanReq->SetIsDirectory(isDir);

        int32_t reqId = GetAvailableRequestId();
        scanReq->SetRequestId(reqId);

        scanExector_.ExecuteScan(move(scanReq));

        // After path validation and queue_ addition is success, add the callback object to callback map
        auto callback = iface_cast<IMediaScannerOperationCallback>(remoteCallback);
        if (callback != nullptr) {
            StoreCallbackObjInMap(reqId, callback);
            errCode = ERR_SUCCESS;
        }
    }

    return errCode;
}

void MediaScanner::CleanupDirectory(const string &path)
{
    vector<int32_t> toBeDeletedIds = {};
    unordered_set<MediaType> mediaTypeSet = {};
    unordered_map<int32_t, MediaType> prevIdMap = {};

    prevIdMap = mediaScannerDb_->GetIdsFromFilePath(path);
    for (auto itr : prevIdMap) {
        auto it = scannedIds_.find(itr.first);
        if (it != scannedIds_.end()) {
            scannedIds_.erase(it);
        } else {
            toBeDeletedIds.push_back(itr.first);
            mediaTypeSet.insert(itr.second);
        }
    }

    // convert deleted id list to vector of strings
    vector<string> deleteIdList;
    for (auto id : toBeDeletedIds) {
        deleteIdList.push_back(to_string(id));
    }
    mediaScannerDb_->DeleteMetadata(deleteIdList);

    // Send notify to the modified URIs
    for (const MediaType &mediaType : mediaTypeSet) {
        mediaScannerDb_->NotifyDatabaseChange(mediaType);
    }

    scannedIds_.clear();
}

int32_t MediaScanner::StartBatchProcessingToDB()
{
    unordered_set<MediaType> mediaTypeSet = {};
    string uri = "";
    Metadata metaData;

    for (uint32_t i = 0; i < batchUpdate_.size(); i++) {
        metaData = (Metadata)batchUpdate_[i];
        mediaTypeSet.insert(metaData.GetFileMediaType());

        if (metaData.GetFileId() != FILE_ID_DEFAULT) {
            uri = mediaScannerDb_->UpdateMetadata(metaData);
            scannedIds_.insert(metaData.GetFileId());
        } else {
            uri = mediaScannerDb_->InsertMetadata(metaData);
            scannedIds_.insert(mediaScannerDb_->GetIdFromUri(uri));
        }
        this->mediaUri_ = uri;
    }
    batchUpdate_.clear();

    // Send notify to the modified URIs
    for (const MediaType &mediaType : mediaTypeSet) {
        mediaScannerDb_->NotifyDatabaseChange(mediaType);
    }

    return ERR_SUCCESS;
}

int32_t MediaScanner::StartBatchProcessIfFull()
{
    if (batchUpdate_.size() >= MAX_BATCH_SIZE) {
        return StartBatchProcessingToDB();
    }
    return ERR_SUCCESS;
}

int32_t MediaScanner::BatchUpdateRequest(Metadata &fileMetadata)
{
    batchUpdate_.push_back(fileMetadata);
    return StartBatchProcessIfFull();
}

// Check if the file entry already exists in the DB. Compare filename, size,
// path, and modified date.
bool MediaScanner::IsFileScanned(Metadata &fileMetadata)
{
    string filePath = fileMetadata.GetFilePath();
    unique_ptr<Metadata> md = mediaScannerDb_->GetFileModifiedInfo(filePath);
    if (md != nullptr &&
        md->GetFileDateModified() == fileMetadata.GetFileDateModified() &&
        md->GetFileName() == fileMetadata.GetFileName() &&
        md->GetFileSize() == fileMetadata.GetFileSize()) {
        scannedIds_.insert(md->GetFileId());
        return true;
    }

    if (md != nullptr) {
        int32_t fileId = md->GetFileId();
        fileMetadata.SetFileId(fileId);
    }

    return false;
}

int32_t MediaScanner::RetrieveMetadata(Metadata &fileMetadata)
{
    // Stub impl. This will be implemented by the extractor
    int32_t errCode = ERR_SUCCESS;
    string path = fileMetadata.GetFilePath();
    errCode = metadataExtract_.Extract(fileMetadata, path);

    return errCode;
}

// Visit the File
int32_t MediaScanner::VisitFile(const Metadata &fileMD)
{
    auto fileMetadata = const_cast<Metadata *>(&fileMD);
    string mimeType = DEFAULT_FILE_MIME_TYPE;
    vector<string> supportedMimeTypes;
    int32_t errCode = ERR_MIMETYPE_NOTSUPPORT;

    mimeType = ScannerUtils::GetMimeTypeFromExtension(fileMetadata->GetFileExtension());
    supportedMimeTypes = GetSupportedMimeTypes();
    if (find(supportedMimeTypes.begin(), supportedMimeTypes.end(), mimeType) != supportedMimeTypes.end()) {
        errCode = ERR_SUCCESS;
        if (!IsFileScanned(*fileMetadata)) {
            errCode = RetrieveMetadata(*fileMetadata);
            if (errCode == ERR_SUCCESS) {
                errCode = BatchUpdateRequest(*fileMetadata);
            }
        }
    }

    return errCode;
}

// Get Basic File Metadata from the file
unique_ptr<Metadata> MediaScanner::GetFileMetadata(const string &path, const int32_t parentId)
{
    unique_ptr<Metadata> fileMetadata = make_unique<Metadata>();
    if (fileMetadata != nullptr) {
        struct stat statInfo = { 0 };
        if (stat(path.c_str(), &statInfo) == ERR_SUCCESS) {
            fileMetadata->SetFileSize(static_cast<int64_t>(statInfo.st_size));
            fileMetadata->SetFileDateAdded(static_cast<int64_t>(statInfo.st_ctime));
            fileMetadata->SetFileDateModified(static_cast<int64_t>(statInfo.st_mtime));
        }

        fileMetadata->SetFilePath(path);
        fileMetadata->SetFileName(ScannerUtils::GetFileNameFromUri(path));

        string fileExtn = ScannerUtils::GetFileExtensionFromFileUri(path);
        fileMetadata->SetFileExtension(fileExtn);

        string mimetype = ScannerUtils::GetMimeTypeFromExtension(fileExtn);
        fileMetadata->SetFileMimeType(mimetype);

        fileMetadata->SetFileMediaType(ScannerUtils::GetMediatypeFromMimetype(mimetype));

        if (parentId != NO_PARENT) {
            fileMetadata->SetParentId(parentId);
        }

        if (ScannerUtils::GetParentPath(path) != ROOT_PATH) {
            fileMetadata->SetAlbumName(ScannerUtils::GetFileNameFromUri(ScannerUtils::GetParentPath(path)));
        }

        size_t found = path.rfind('/');
        if (found != string::npos) {
            fileMetadata->SetRelativePath(path.substr(0, found));
        }
    }

    return fileMetadata;
}

// Get the internal details of the file
int32_t MediaScanner::ScanFileContent(const string &path, const int32_t parentId)
{
    unique_ptr<Metadata> fileMetadata = nullptr;
    int32_t errCode = ERR_FAIL;

    fileMetadata = GetFileMetadata(path, parentId);
    if (fileMetadata != nullptr) {
        errCode = VisitFile(*fileMetadata);
    } else {
        return ERR_MEM_ALLOC_FAIL;
    }

    return errCode;
}

// Scan the file path
int32_t MediaScanner::ScanFileInternal(const string &path)
{
    int32_t errCode = ERR_FAIL;

    string parentFolder = ScannerUtils::GetParentPath(path);
    if (ScannerUtils::IsFileHidden(path) || (!parentFolder.empty() && IsDirHiddenRecursive(parentFolder))) {
        MEDIA_ERR_LOG("File Parent path not accessible");
        return ERR_NOT_ACCESSIBLE;
    }

    int32_t parentId = mediaScannerDb_->ReadAlbumId(parentFolder);

    errCode = ScanFileContent(path, parentId);
    if (errCode == ERR_SUCCESS) {
        // to write the remaining to DB
        errCode = StartBatchProcessingToDB();
    }

    return errCode;
}

int32_t MediaScanner::InsertAlbumInfo(string &albumPath, int32_t parentId, string &albumName)
{
    int32_t errCode = ERR_MEM_ALLOC_FAIL;

    bool update = false;

    // This will be empty for the first folder in dir path
    if (albumMap_.empty()) {
        mediaScannerDb_->ReadAlbums(albumPath, albumMap_);
    }

    if (albumMap_.find(albumPath) != albumMap_.end()) {
        // It Exists
        Metadata albumInfo = albumMap_.at(albumPath);
        struct stat statInfo {};
        if (stat(albumPath.c_str(), &statInfo) == ERR_SUCCESS) {
            if (albumInfo.GetFileDateModified() == statInfo.st_mtime) {
                errCode = albumInfo.GetFileId();
                return errCode;
            } else {
                update = true;
            }
        }
    }

    unique_ptr<Metadata> fileMetadata = GetFileMetadata(albumPath, parentId);
    if (fileMetadata != nullptr) {
        fileMetadata->SetFileMediaType(static_cast<MediaType>(MEDIA_TYPE_ALBUM));
        fileMetadata->SetAlbumName(albumName);

        if (update) {
            errCode = mediaScannerDb_->UpdateAlbum(*fileMetadata);
        } else {
            errCode = mediaScannerDb_->InsertAlbum(*fileMetadata);
        }
    }

    return errCode;
}

int32_t MediaScanner::WalkFileTree(const string &path, int32_t parentId)
{
    int32_t errCode = ERR_SUCCESS;
    DIR *dirPath = nullptr;
    struct dirent *ent = nullptr;
    int32_t len = path.length();
    struct stat statInfo;

    if (len >= FILENAME_MAX - 1) {
        return ERR_INCORRECT_PATH;
    }

    if ((dirPath = opendir(path.c_str())) == nullptr) {
        return ERR_NOT_ACCESSIBLE;
    }

    auto fName = (char *)calloc(FILENAME_MAX, sizeof(char));
    if (fName == nullptr) {
        return ERR_MEM_ALLOC_FAIL;
    }

    if (strcpy_s(fName, FILENAME_MAX, path.c_str()) != ERR_SUCCESS) {
        free(fName);
        return ERR_MEM_ALLOC_FAIL;
    }
    fName[len++] = '/';

    while ((ent = readdir(dirPath)) != nullptr && errCode != ERR_MEM_ALLOC_FAIL) {
        if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, "..")) {
            continue;
        }

        strncpy_s(fName + len, FILENAME_MAX, ent->d_name, FILENAME_MAX - len);
        if (lstat(fName, &statInfo) == -1) {
            MEDIA_ERR_LOG("Obtaining Stat info failed");
            continue;
        }

        string currentPath = fName;

        if (S_ISDIR(statInfo.st_mode)) {
            // Insert folder info to database
            string albumName = ent->d_name;
            int32_t albumId = InsertAlbumInfo(currentPath, parentId, albumName);
            if (!IsDirHidden(currentPath)) {
                errCode = WalkFileTree(currentPath, albumId);
            }
        } else if (!ScannerUtils::IsFileHidden(currentPath)) {
            errCode = ScanFileContent(currentPath, parentId);
        }
    }

    closedir(dirPath);
    free(fName);

    return errCode;
}

// Initialize the skip list
void MediaScanner::InitSkipList()
{
    hash<string> hashStr;
    size_t hashPath;
    string path;

    ifstream skipFile(SKIPLIST_FILE_PATH.c_str());
    if (skipFile.is_open()) {
        while (getline(skipFile, path)) {
            hashPath = hashStr(path);
            skipList_.insert(skipList_.begin(), hashPath);
        }
        skipFile.close();
    }

    return;
}

// Check if path is part of Skip scan list
bool MediaScanner::CheckSkipScanList(const string &path)
{
    hash<string> hashStr;
    size_t hashPath;

    if (skipList_.empty()) {
        InitSkipList();
    }

    hashPath = hashStr(path);
    if (find(skipList_.begin(), skipList_.end(), hashPath) != skipList_.end()) {
        return true;
    }

    return false;
}

// Check if the directory is hidden
bool MediaScanner::IsDirHidden(const string &path)
{
    bool dirHid = false;

    if (!path.empty()) {
        string dirName = ScannerUtils::GetFileNameFromUri(path);
        if (dirName.at(0) == '.') {
            return true;
        }

        string curPath = path;
        string excludePath = curPath.append("/.nomedia");
        // Check is the folder consist of .nomedia file
        if (ScannerUtils::IsExists(excludePath)) {
            return true;
        }

        // Check is the dir is part of skiplist
        if (CheckSkipScanList(path)) {
            return true;
        }
    }

    return dirHid;
}

// Check if the dir is hidden
bool MediaScanner::IsDirHiddenRecursive(const string &path)
{
    bool dirHid = false;
    string curPath = path;

    do {
        dirHid = IsDirHidden(curPath);
        if (dirHid) {
            break;
        }

        curPath = ScannerUtils::GetParentPath(curPath);
        if (ROOT_PATH == curPath) {
            break;
        }
    } while (true);

    return dirHid;
}

// Scan the directory path recursively
int32_t MediaScanner::ScanDirInternal(const string &path)
{
    int32_t errCode = ERR_FAIL;

    // Check if it is hidden folder
    if (IsDirHiddenRecursive(path)) {
        return ERR_NOT_ACCESSIBLE;
    }

    // Walk the folder tree
    errCode = WalkFileTree(path, NO_PARENT);
    if (errCode == ERR_SUCCESS) {
        // write the remaining metadata to DB
        errCode = StartBatchProcessingToDB();
        if (errCode == ERR_SUCCESS) {
            CleanupDirectory(path);
        }
    }

    return errCode;
}

void MediaScanner::ExecuteScannerClientCallback(int32_t reqId, int32_t status, const string &uri, const string &path)
{
    auto iter = scanResultCbMap_.find(reqId);
    if (iter != scanResultCbMap_.end()) {
        auto activeCb = iter->second;
        activeCb->OnScanFinishedCallback(status, uri, path);
        scanResultCbMap_.erase(iter);
    }
}

void MediaScanner::StoreCallbackObjInMap(int32_t reqId, sptr<IMediaScannerOperationCallback>& callback)
{
    auto itr = scanResultCbMap_.find(reqId);
    if (itr == scanResultCbMap_.end()) {
        scanResultCbMap_.insert(std::make_pair(reqId, callback));
    }
}

int32_t MediaScanner::GetAvailableRequestId()
{
    static int32_t i = 0;
    if (scanResultCbMap_.empty()) {
        i = 0;
    }

    return ++i;
}

void MediaScanner::SetAbilityContext(const std::shared_ptr<Context> &context)
{
    abilityContext_ = context;
}

void MediaScanner::ReleaseAbilityHelper()
{
    if (rdbhelper_ != nullptr) {
        rdbhelper_->Release();
        rdbhelper_ = nullptr;
    }
}
} // namespace Media
} // namespace OHOS
