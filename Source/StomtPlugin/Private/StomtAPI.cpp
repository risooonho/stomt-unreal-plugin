// Copyright 2018 STOMT GmbH. All Rights Reserved.
#pragma once

#include "StomtAPI.h"
#include "StomtPluginPrivatePCH.h"
#include "StomtJsonObject.h"

#include "Runtime/Engine/Public/HighResScreenshot.h"
#include "Runtime/Core/Public/Misc/FileHelper.h"
#include "Runtime/Core/Public/Misc/Paths.h"
#include "Runtime/Core/Public/GenericPlatform/GenericPlatformFile.h"
#include "Runtime/Core/Public/Misc/Base64.h"
#include "Runtime/Core/Public/Misc/App.h"
#include "Runtime/Core/Public/Internationalization/Regex.h"

UStomtAPI* UStomtAPI::ConstructStomtAPI(FString TargetID, FString RestURL, FString AppID)
{
	UStomtAPI* api = NewObject<UStomtAPI>();

	if (AppID.Equals("AKN5M7Ob0MqxKXYdE9i3IhQtF"))
	{
		api->SetRestURL("https://test.rest.stomt.com");
	}
	else
	{
		api->SetRestURL("https://rest.stomt.com");
	}

	api->SetAppID(AppID);

	UE_LOG(StomtInit, Log, TEXT("Construct Stomt API"));
	UE_LOG(StomtInit, Log, TEXT("AppID: %s "), *api->GetAppID());
	UE_LOG(StomtInit, Log, TEXT("RestURL: %s "), *api->GetRestURL());

	api->LoadLanguageFile();

	UE_LOG(StomtInit, Log, TEXT("LangTest: %s"), *api->GetLangText("STOMT_WISH_BUBBLE"));

	return api;
}

UStomtAPI::UStomtAPI()
{
	LogFileWasSend = false;
	EMailFlagWasSend = false;
	IsImageUploadComplete = false;
	IsLogUploadComplete = false;
	UseImageUpload = true;
	useDefaultLabels = true;

	this->Config = UStomtConfig::ConstructStomtConfig();
	this->Track = UStomtTrack::ConstructStomtTrack();
	DefaultScreenshotName = FString("HighresScreenshot00000.png");

	 this->SetCurrentLanguage(this->GetSystemLanguage());
}

UStomtAPI::~UStomtAPI()
{
}

void UStomtAPI::SendStomt(UStomt* stomt)
{
	UStomtRestRequest* request = this->SetupNewPostRequest();
	request->OnRequestComplete.AddDynamic(this, &UStomtAPI::OnSendStomtRequestResponse);

	// Fields
	request->GetRequestObject()->SetField(TEXT("target_id"),	UStomtJsonValue::ConstructJsonValueString(	this, stomt->GetTargetID()	));
	request->GetRequestObject()->SetField(TEXT("positive"),	UStomtJsonValue::ConstructJsonValueBool(	this, stomt->GetPositive()	));
	request->GetRequestObject()->SetField(TEXT("text"),		UStomtJsonValue::ConstructJsonValueString(	this, stomt->GetText()		));
	request->GetRequestObject()->SetField(TEXT("anonym"),		UStomtJsonValue::ConstructJsonValueBool(	this, stomt->GetAnonym()	));
	
	//Labels
	UStomtRestJsonObject* jObjExtraData = UStomtRestJsonObject::ConstructJsonObject(this);
	TArray<UStomtJsonValue*> labels = TArray<UStomtJsonValue*>();

	for (int i = 0; i != stomt->GetLabels().Num(); ++i)
	{
		if (!stomt->GetLabels()[i]->GetName().IsEmpty())
		{
			labels.Add(UStomtJsonValue::ConstructJsonValueString(this, stomt->GetLabels()[i]->GetName()));
		}
	}

	if (useDefaultLabels)
	{
		const FVector2D ViewportSize = FVector2D(GEngine->GameViewport->Viewport->GetSizeXY());
		labels.Add(UStomtJsonValue::ConstructJsonValueString(this, ViewportSize.ToString()));
		labels.Add(UStomtJsonValue::ConstructJsonValueString(this, UGameplayStatics::GetPlatformName()));
	}

	if (labels.Num() > 0)
	{
		jObjExtraData->SetArrayField(TEXT("labels"), labels);
		request->GetRequestObject()->SetObjectField(TEXT("extradata"), jObjExtraData);
	}

	if (CustomKeyValuePairs.Num() > 0)
	{
		for (int i = 0; i != CustomKeyValuePairs.Num(); ++i)
		{
			jObjExtraData->SetStringField(CustomKeyValuePairs[i][0], CustomKeyValuePairs[i][1]);
		}	
	}

	// Stomt Image
	if (!this->ImageUploadName.IsEmpty() && UseImageUpload)
	{
		request->GetRequestObject()->SetStringField(TEXT("img_name"), ImageUploadName);
		UE_LOG(StomtNetwork, Log, TEXT("Append Image"));
	}
	else
	{
		UE_LOG(StomtNetwork, Log, TEXT("Append no Image (Don't use image upload)"));
	}

	// Error Logs
	if (!this->errorLog_file_uid.IsEmpty())
	{
		UStomtRestJsonObject* jObjFile = UStomtRestJsonObject::ConstructJsonObject(this);
		UStomtRestJsonObject* jObjFileContext = UStomtRestJsonObject::ConstructJsonObject(this);
		jObjFileContext->SetField(TEXT("file_uid"), UStomtJsonValue::ConstructJsonValueString(this, this->errorLog_file_uid));

		jObjFile->SetObjectField(TEXT("stomt"), jObjFileContext);
		request->GetRequestObject()->SetObjectField(TEXT("files"), jObjFile);
	}

	request->ProcessURL( this->GetRestURL().Append(TEXT("/stomts")) );
}

void UStomtAPI::OnSendStomtRequestResponse(UStomtRestRequest * Request)
{
	if (Request->GetResponseCode() == 200)
	{
		if (Request->GetResponseObject()->HasField(TEXT("data")))
		{
			if (Request->GetResponseObject()->GetObjectField(TEXT("data"))->HasField(TEXT("id")))
			{
				FString id = Request->GetResponseObject()->GetObjectField(TEXT("data"))->GetStringField(TEXT("id"));
				this->Track->SetStomtID(id);
				this->Track->SetEventCategory("stomt");
				this->Track->SetEventAction("submit");
				this->SendTrack(this->Track);
			}
		}
	}
	else
	{
		// DoTo react to this
		UE_LOG(StomtNetwork, Warning, TEXT("Send Stomt did not work | OnSendStomtRequestResponse"));
	}
}

UStomtRestRequest* UStomtAPI::SendLoginRequest(FString UserName, FString Password)
{
	UStomtRestRequest* request = this->SetupNewPostRequest();
	request->OnRequestComplete.AddDynamic(this, &UStomtAPI::OnLoginRequestResponse);
	request->OnRequestFail.AddDynamic(this, &UStomtAPI::OnARequestFailed);

	request->UseRequestLogging(false);

	request->GetRequestObject()->SetStringField(TEXT("login_method"), TEXT("normal"));
	request->GetRequestObject()->SetStringField(TEXT("emailusername"), UserName);
	request->GetRequestObject()->SetStringField(TEXT("password"), Password);

	request->ProcessURL(this->GetRestURL().Append(TEXT("/authentication/session")));

	request->UseRequestLogging(true);

	return request;
}

void UStomtAPI::OnLoginRequestResponse(UStomtRestRequest * Request)
{
	if (Request->GetResponseCode() == 200)
	{
		if (Request->GetResponseObject()->HasField(TEXT("data")))
		{
			if (Request->GetResponseObject()->GetObjectField(TEXT("data"))->HasField(TEXT("accesstoken")))
			{
				FString Accesstoken = Request->GetResponseObject()->GetObjectField(TEXT("data"))->GetStringField(TEXT("accesstoken"));
				this->Config->SetAccessToken(Accesstoken);
				this->Config->SetSubscribed(true);
				this->Config->SetLoggedIn(true);

				this->Track->SetEventCategory("auth");
				this->Track->SetEventAction("login");
				this->SendTrack(this->Track);
			}
		}
	}
}

void UStomtAPI::SendStomtLabels(UStomt * stomt)
{
	return;
	//ToDo: Finish this some day. Works only with target owner login.

	/*
	if (!stomt->GetServersideID().IsEmpty() && stomt->GetLabels().Max() > 0)
	{
		//Reset Request
		this->Request->ResetResponseData();
		this->Request->ResetRequestData();

		UE_LOG(LogTemp, Warning, TEXT("nice1"));
		this->Request->SetVerb(ERequestVerb::POST);
		this->Request->SetHeader(TEXT("appid"), this->GetAppID() );

		this->Request->GetRequestObject()->SetField(TEXT("name"),	UStomtJsonValue::ConstructJsonValueString(	this, TEXT("newlabeltest")	));
		this->Request->GetRequestObject()->SetField(TEXT("as_target_owner"), UStomtJsonValue::ConstructJsonValueString(this, TEXT("true")));

		this->Request->ProcessURL( this->GetRestURL().Append(TEXT("/stomts/")).Append(stomt->GetServersideID()).Append(TEXT("/labels") ) );
	}
	*/
}

UStomtRestRequest* UStomtAPI::RequestSession(FString Accesstoken)
{
	if (Accesstoken.IsEmpty())
	{
		return NULL;
	}

	UStomtRestRequest* request = NewObject<UStomtRestRequest>();
	request->OnRequestComplete.AddDynamic(this, &UStomtAPI::OnRequestSessionResponse);
	request->OnRequestFail.AddDynamic(this, &UStomtAPI::OnARequestFailed);

	request->SetVerb(ERequestVerb::GET);
	request->SetHeader(TEXT("appid"), this->GetAppID());
	
	request->SetHeader(TEXT("accesstoken"), this->Config->GetAccessToken());
	UE_LOG(StomtNetwork, Log, TEXT("RequestSession: AddAccesstoken: %s "), *this->Config->GetAccessToken());

	request->ProcessURL(this->GetRestURL().Append("/authentication/session"));

	return request;
}

void UStomtAPI::OnRequestSessionResponse(UStomtRestRequest * Request)
{
	if (Request->GetResponseCode() == 419)
	{
		// Forbidden: Session invalid. (Request a new access-token via login or refresh token.)

		UE_LOG(StomtNetwork, Warning, TEXT("Invalid accesstoken."));

		Config->SetAccessToken("");
	}

	if (Request->GetResponseCode() != 200) return;

	if (!Request->GetResponseObject()->HasField(TEXT("data"))) return;

	if (!Request->GetResponseObject()->GetObjectField(TEXT("data"))->HasField(TEXT("user"))) return;

	this->StomtsCreatedByUser = (int)Request->GetResponseObject()->GetObjectField(TEXT("data"))->GetObjectField(TEXT("user"))->GetObjectField(TEXT("stats"))->GetNumberField(TEXT("amountStomtsCreated"));
	this->UserID = (FString)Request->GetResponseObject()->GetObjectField(TEXT("data"))->GetObjectField(TEXT("user"))->GetStringField("id");

	OnSessionRequestComplete.Broadcast(Request);

	UE_LOG(StomtNetwork, Log, TEXT("StomtsCreatedByUser: %d | StomtsReceivedByTarget: %d"), StomtsCreatedByUser, StomtsReceivedByTarget);
}

UStomtRestRequest * UStomtAPI::RequestTargetByAppID()
{
	UStomtRestRequest* request = NewObject<UStomtRestRequest>();
	request->OnRequestComplete.AddDynamic(this, &UStomtAPI::OnRequestTargetResponse);
	request->OnRequestFail.AddDynamic(this, &UStomtAPI::OnARequestFailed);

	request->SetVerb(ERequestVerb::GET);
	request->SetHeader(TEXT("appid"), this->GetAppID());

	request->ProcessURL(this->GetRestURL().Append("/targets/"));

	return request;
}

UStomtRestRequest* UStomtAPI::RequestTarget(FString TargetID)
{
	UStomtRestRequest* request = NewObject<UStomtRestRequest>();
	request->OnRequestComplete.AddDynamic(this, &UStomtAPI::OnRequestTargetResponse);
	request->OnRequestFail.AddDynamic(this, &UStomtAPI::OnARequestFailed);

	request->SetVerb(ERequestVerb::GET);
	request->SetHeader(TEXT("appid"), this->GetAppID());

	request->ProcessURL(this->GetRestURL().Append("/targets/").Append(TargetID));

	return request;
}

void UStomtAPI::OnRequestTargetResponse(UStomtRestRequest * Request)
{
	if (Request->GetResponseCode() != 200) return;

	if (!Request->GetResponseObject()->HasField(TEXT("data"))) return;
	
	if (!Request->GetResponseObject()->GetObjectField(TEXT("data"))->HasField(TEXT("displayname"))) return;
	
	this->TargetName = Request->GetResponseObject()->GetObjectField(TEXT("data"))->GetStringField(TEXT("displayname"));
	this->TargetID = Request->GetResponseObject()->GetObjectField(TEXT("data"))->GetStringField(TEXT("id"));
	this->SetImageURL(Request->GetResponseObject()
		->GetObjectField(TEXT("data"))
		->GetObjectField(TEXT("images"))
		->GetObjectField(TEXT("profile"))
		->GetStringField(TEXT("url")));

	this->StomtsReceivedByTarget = (int)Request->GetResponseObject()
		->GetObjectField(TEXT("data"))
		->GetObjectField(TEXT("stats"))
		->GetNumberField(TEXT("amountStomtsReceived"));

	if (!this->Config->GetAccessToken().IsEmpty())
	{
		this->RequestSession(this->Config->GetAccessToken());
	}
}

void UStomtAPI::SetRestURL(FString URL)
{
	this->RestURL = URL;
}

FString UStomtAPI::GetRestURL()
{
	return this->RestURL;
}

void UStomtAPI::SetAppID(FString appID)
{
	this->AppID = appID;
}

FString UStomtAPI::GetAppID()
{
	return this->AppID;
}

FString UStomtAPI::GetTargetName()
{
	return this->TargetName;
}

void UStomtAPI::SetTargetID(FString targetID)
{
	this->TargetID = targetID;
}

FString UStomtAPI::GetTargetID()
{
	return this->TargetID;
}

void UStomtAPI::SetImageURL(FString URL)
{
	this->ImageURL = URL;
}

FString UStomtAPI::GetImageURL()
{
	return this->ImageURL;
}

void UStomtAPI::SetStomtToSend(UStomt * stomt)
{
	this->StomtToSend = stomt;
}

FString UStomtAPI::ReadLogFile(FString LogFileName)
{
	FString errorLog;

	FString LogFilePath = FPaths::ProjectLogDir() + LogFileName;
	FString LogFileCopyPath = FPaths::ProjectLogDir() + LogFileName + TEXT("Copy.log");
	FString LogFileCopyName = LogFileName + TEXT("Copy.log");

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	PlatformFile.BypassSecurity(true);

	// Copy LogFileData
	if (!PlatformFile.CopyFile(*LogFileCopyPath, *LogFilePath, EPlatformFileRead::AllowWrite, EPlatformFileWrite::AllowRead))
	{
		UE_LOG(StomtFileAccess, Error, TEXT("LogFile Copy did not work FromFile: %s | ToFile %s"), *LogFilePath, *LogFileCopyPath);
	}

	// Read LogFileCopy from Disk
	if (!this->ReadFile(errorLog, LogFileCopyName, FPaths::ProjectLogDir() ))
	{
		if (FPaths::FileExists(FPaths::ProjectLogDir() + LogFileCopyName))
		{
			UE_LOG(StomtFileAccess, Warning, TEXT("Could not read LogFile %s, but it exists"), *LogFileCopyName);
		}
		else
		{
			UE_LOG(StomtFileAccess, Warning, TEXT("Could not read LogFile %s, because it does not exist"), *LogFileCopyName);
		}
	}

	// Delete LogFileCopy
	if (!PlatformFile.DeleteFile(*LogFileCopyPath))
	{
		UE_LOG(StomtFileAccess, Warning, TEXT("Could not delete LogFileCopy %s"), *LogFileCopyPath);
	}

	return errorLog;
}

void UStomtAPI::SendLogFile(FString LogFileData, FString LogFileName)
{
	UStomtRestRequest* request = this->SetupNewPostRequest();
	request->OnRequestComplete.AddDynamic(this, &UStomtAPI::OnSendLogFileResponse);

	FString logJson = FString(TEXT("{ \"files\": { \"stomt\": [ { \"data\":\"") + FBase64::Encode(LogFileData) + TEXT("\", \"filename\" : \"") + LogFileName + TEXT("\" } ] } }"));

	request->UseStaticJsonString(true);
	request->SetStaticJsonString(logJson);

	request->ProcessURL(this->GetRestURL().Append(TEXT("/files")));

	LogFileWasSend = true;
}

void UStomtAPI::OnSendLogFileResponse(UStomtRestRequest * Request)
{
	// Request access token
	if (this->Config->GetAccessToken().IsEmpty())
	{
		if (Request->GetResponseObject()->HasField(TEXT("meta")))
		{
			if (Request->GetResponseObject()->GetObjectField(TEXT("meta"))->HasField(TEXT("accesstoken")))
			{
				FString Accesstoken = Request->GetResponseObject()->GetObjectField(TEXT("meta"))->GetStringField(TEXT("accesstoken"));
				this->Config->SetAccessToken(Accesstoken);
			}
		}
	}

	// Get File uid of the error log and send stomt
	if (Request->GetResponseObject()->HasField(TEXT("data")))
	{
		if (Request->GetResponseObject()->GetObjectField(TEXT("data"))->HasField(TEXT("files")))
		{
			this->errorLog_file_uid = Request->GetResponseObject()->GetObjectField(TEXT("data"))->GetObjectField(TEXT("files"))->GetObjectField(TEXT("stomt"))->GetStringField("file_uid");
			this->LogFileWasSend = false;

			if (IsImageUploadComplete)
			{
				this->SendStomt(StomtToSend);
				UE_LOG(StomtNetwork, Log, TEXT("Sent Stomt after sending log files"));
			}
			else
			{
				UE_LOG(StomtNetwork, Log, TEXT("Did not send stomt | image upload not complete"));
			}
		}
	}

	if (Request->GetResponseCode() == 403 || Request->GetResponseCode() == 419 || Request->GetResponseCode() == 413)
	{
		if (StomtToSend != NULL)
		{
			this->SendStomt(StomtToSend);
			LogFileWasSend = false;
		}
	}

	IsLogUploadComplete = true;
}

void UStomtAPI::SendImageFile(FString ImageFileDataBase64)
{
	if (ImageFileDataBase64.IsEmpty())
	{
		UE_LOG(StomtNetwork, Warning, TEXT("Could not send stomt image | ImageFileDataBase64 was empty"));
		return;
	}

	UStomtRestRequest* request = this->SetupNewPostRequest();
	request->OnRequestComplete.AddDynamic(this, &UStomtAPI::OnSendImageFileResponse);

	FString ImageJson = FString(TEXT("{ \"images\": { \"stomt\": [ { \"data\":\"") + ImageFileDataBase64 + TEXT("\" } ] } }"));

	request->UseStaticJsonString(true);
	request->SetStaticJsonString(ImageJson);

	request->ProcessURL(this->GetRestURL().Append(TEXT("/images")));

}

void UStomtAPI::OnSendImageFileResponse(UStomtRestRequest * Request)
{
	// Request access token
	if (this->Config->GetAccessToken().IsEmpty())
	{
		if (Request->GetResponseObject()->HasField(TEXT("meta")))
		{
			if (Request->GetResponseObject()->GetObjectField(TEXT("meta"))->HasField(TEXT("accesstoken")))
			{
				FString Accesstoken = Request->GetResponseObject()->GetObjectField(TEXT("meta"))->GetStringField(TEXT("accesstoken"));
				this->Config->SetAccessToken(Accesstoken);
			}
		}
	}

	// Get File uid of the error log and send stomt
	if (Request->GetResponseObject()->HasField(TEXT("data")))
	{
		if (Request->GetResponseObject()->GetObjectField(TEXT("data"))->HasField(TEXT("images")))
		{
			this->ImageUploadName = Request->GetResponseObject()->GetObjectField(TEXT("data"))->GetObjectField(TEXT("images"))->GetObjectField(TEXT("stomt"))->GetStringField("name");
			UE_LOG(StomtFileAccess, Log, TEXT("Image Upload complete %s"), *this->ImageUploadName);
		}
	}

	if (Request->GetResponseCode() == 403 || Request->GetResponseCode() == 419 || Request->GetResponseCode() == 413)
	{
		if (StomtToSend != NULL)
		{
			//this->SendStomt(StomtToSend);
			//LogFileWasSend = false;
		}
	}

	if (IsLogUploadComplete)
	{
		this->SendStomt(StomtToSend);
		UE_LOG(StomtNetwork, Log, TEXT("Sent Stomt after sending screenshot file"));
	}
	else
	{
		UE_LOG(StomtNetwork, Log, TEXT("Did not send stomt | log upload not complete"));
	}

	IsImageUploadComplete = true;
}

void UStomtAPI::SendSubscription(FString EMail)
{
	SendSubscription(EMail, false);
}

void UStomtAPI::SendSubscription(FString EMailOrNumber, bool UseEmail)
{
	UStomtRestRequest* request = this->SetupNewPostRequest();
	request->OnRequestComplete.AddDynamic(this, &UStomtAPI::UStomtAPI::OnSendEMailResponse);

	if (UseEmail)
	{
		request->GetRequestObject()->SetStringField(TEXT("email"), EMailOrNumber);
	}
	else
	{
		request->GetRequestObject()->SetStringField(TEXT("phone"), EMailOrNumber);
	}

	request->ProcessURL(this->GetRestURL().Append(TEXT("/authentication/subscribe")));
}

void UStomtAPI::OnSendEMailResponse(UStomtRestRequest * Request)
{
	if (Request->GetResponseCode() == 200)
	{
		if (Request->GetResponseObject()->HasField(TEXT("data")))
		{
			if (Request->GetResponseObject()->GetObjectField(TEXT("data"))->HasField(TEXT("success")))
			{
				if (Request->GetResponseObject()->GetObjectField(TEXT("data"))->GetBoolField("success"))
				{
					this->Config->SetSubscribed(true);
					this->Track->SetEventCategory("auth");
					this->Track->SetEventAction("subscribed");
					this->SendTrack(this->Track);
				}
			}
		}
	}
	else
	{
		this->Config->SetSubscribed(false);
	}
}

void UStomtAPI::SendLogoutRequest()
{
	UStomtRestRequest* request = this->SetupNewDeleteRequest();
	request->OnRequestComplete.AddDynamic(this, &UStomtAPI::UStomtAPI::OnSendLogoutResponse);

	request->ProcessURL(this->GetRestURL().Append(TEXT("/authentication/session")));

	this->Config->SetAccessToken(TEXT(""));
	this->Config->SetLoggedIn(false);
	this->Config->SetSubscribed(false);
}

void UStomtAPI::OnSendLogoutResponse(UStomtRestRequest * Request)
{
	if (Request->GetResponseCode() == 200)
	{
		if (Request->GetResponseObject()->HasField(TEXT("data")))
		{
			if (Request->GetResponseObject()->GetObjectField(TEXT("data"))->HasField(TEXT("success")))
			{
				if (Request->GetResponseObject()->GetObjectField(TEXT("data"))->GetBoolField("success"))
				{
					return; // logout was successful
				}
			}
		}
	}

	UE_LOG(StomtNetwork, Warning, TEXT("Logout failed"));
}

void UStomtAPI::SendTrack(UStomtTrack * Track)
{
	UStomtRestRequest* request = this->SetupNewPostRequest();

	// Add target id
	Track->SetTargetID(this->GetTargetID());

	request->SetRequestObject(Track->GetAsJsonObject());

	request->ProcessURL(this->GetRestURL().Append(TEXT("/tracks")));
}

FString UStomtAPI::ReadScreenshotAsBase64()
{
	FString ScreenDir = FPaths::ScreenShotDir();
	FString FilePath = ScreenDir + this->DefaultScreenshotName;

	UE_LOG(Stomt, Log, TEXT("TakeScreenshot | FilePath: %s"), *FilePath);
	UE_LOG(Stomt, Log, TEXT("Screenshot | AllocatedSize: %d"), this->ReadBinaryFile(FilePath).GetAllocatedSize());

	TArray<uint8> file = this->ReadBinaryFile(FilePath);

	//Delete Screenshot
	FString AbsoluteFilePath = ScreenDir + this->DefaultScreenshotName;
	if (!FPlatformFileManager::Get().GetPlatformFile().DeleteFile(*AbsoluteFilePath))
	{
		UE_LOG(StomtFileAccess, Warning, TEXT("Could not delete old screenshot File (Could Not Find Screenshot File)") );
	}

	return FBase64::Encode(file);
}

void UStomtAPI::OnARequestFailed(UStomtRestRequest * Request)
{
	this->OnRequestFailed.Broadcast(Request);
}

UStomtRestJsonObject* UStomtAPI::LoadLanguageFile()
{
	FString jsonString = this->LoadLanguageFileContent();
	UStomtRestJsonObject* jsonObject = UStomtRestJsonObject::ConstructJsonObject(this);
	if (jsonObject->DecodeJson(jsonString))
	{
		this->Languages = jsonObject;
	}
	else
	{
		UE_LOG(StomtInit, Error, TEXT("Could not decode Language File StomtPlugin(Sub)/Resources/languages.json"));
	}

	return jsonObject;
}

FString UStomtAPI::LoadLanguageFileContent()
{
	FString jsonString = "";

	// Try Marketplace Plugin Folder - EnginePluginsDir()/Marketplace
	if (this->ReadFile(jsonString, FString(TEXT("languages.json")), FPaths::EnginePluginsDir() + "Marketplace/StomtPlugin/Resources/")) {
		return jsonString;
	}
	if (this->ReadFile(jsonString, FString(TEXT("languages.json")), FPaths::EnginePluginsDir() + "Marketplace/StomtPluginSub/Resources/")) {
		return jsonString;
	}
	if (this->ReadFile(jsonString, FString(TEXT("languages.json")), FPaths::EnginePluginsDir() + "Marketplace/stomt-unreal-plugin/Resources/")) {
		return jsonString;
	}

	// Try Engine Plugin Folder - EnginePluginsDir()
	if (this->ReadFile(jsonString, FString(TEXT("languages.json")), FPaths::EnginePluginsDir() + "StomtPlugin/Resources/")) {
		return jsonString;
	}
	if (this->ReadFile(jsonString, FString(TEXT("languages.json")), FPaths::EnginePluginsDir() + "StomtPluginSub/Resources/")) {
		return jsonString;
	}
	if (this->ReadFile(jsonString, FString(TEXT("languages.json")), FPaths::EnginePluginsDir() + "stomt-unreal-plugin/Resources/")) {
		return jsonString;
	}

	// Try local Game Plugin Folder - GamePluginsDir()
	if (this->ReadFile(jsonString, FString(TEXT("languages.json")), FPaths::GamePluginsDir() + "StomtPlugin/Resources/")) {
		return jsonString;
	}
	if (this->ReadFile(jsonString, FString(TEXT("languages.json")), FPaths::GamePluginsDir() + "StomtPluginSub/Resources/")) {
		return jsonString;
	}
	if (this->ReadFile(jsonString, FString(TEXT("languages.json")), FPaths::GamePluginsDir() + "stomt-unreal-plugin/Resources/")) {
		return jsonString;
	}

	// Try local Project Plugin Folder - ProjectPluginsDir()
	if (this->ReadFile(jsonString, FString(TEXT("languages.json")), FPaths::ProjectPluginsDir() + "StomtPlugin/Resources/")) {
		return jsonString;
	}
	if (this->ReadFile(jsonString, FString(TEXT("languages.json")), FPaths::ProjectPluginsDir() + "StomtPluginSub/Resources/")) {
		return jsonString;
	}
	if (this->ReadFile(jsonString, FString(TEXT("languages.json")), FPaths::ProjectPluginsDir() + "stomt-unreal-plugin/Resources/")) {
		return jsonString;
	}

	// Try local Enterprise Plugin Folder - EnterprisePluginsDir()
	if (this->ReadFile(jsonString, FString(TEXT("languages.json")), FPaths::EnterprisePluginsDir() + "StomtPlugin/Resources/")) {
		return jsonString;
	}
	if (this->ReadFile(jsonString, FString(TEXT("languages.json")), FPaths::EnterprisePluginsDir() + "StomtPluginSub/Resources/")) {
		return jsonString;
	}
	if (this->ReadFile(jsonString, FString(TEXT("languages.json")), FPaths::EnterprisePluginsDir() + "stomt-unreal-plugin/Resources/")) {
		return jsonString;
	}

	UE_LOG(StomtInit, Log, TEXT("Unable to find languages.json file."));
	return jsonString;
}

FString UStomtAPI::GetLangText(FString text)
{
	if (this->CurrentLanguage.IsEmpty())
	{
		this->CurrentLanguage = "en";
	}

	if (this->Languages != NULL)
	{
		if (!this->Languages->GetObjectField("data")->HasField(this->CurrentLanguage))
		{
			UE_LOG(StomtNetwork, Warning, TEXT("Language %s not supported (does not exist in language file) falling back to english."), *this->CurrentLanguage);
			this->CurrentLanguage = "en";
		}

		if ( !this->Languages->GetObjectField("data")->GetObjectField(this->CurrentLanguage)->HasField(text) )
		{
			UE_LOG(StomtNetwork, Warning, TEXT("Translation for '%s' not found in language: '%s'."), *text, *this->CurrentLanguage);
			return "No Transl.";
		}

		return this->Languages->GetObjectField("data")->GetObjectField(this->CurrentLanguage)->GetStringField(text);
	}

	return FString();
}

FString UStomtAPI::GetCurrentLanguage()
{
	return this->CurrentLanguage;
}

bool UStomtAPI::SetCurrentLanguage(FString language)
{
	if (language.IsEmpty())
		return false;

	this->CurrentLanguage = language.Left(2);

	return true;
}

FString UStomtAPI::GetSystemLanguage()
{
	//UE_LOG(StomtFileAccess, Warning, TEXT("Culture: %s "), *FInternationalization::Get().GetCurrentCulture()->GetName().Left(2));
	return FInternationalization::Get().GetCurrentCulture()->GetName().Left(2);
}

bool UStomtAPI::IsEmailCorrect(FString Email)
{
	const FRegexPattern pattern(TEXT("^[a-zA-Z0-9.!#$%&'*+/=?^_`{|}~-]+@[a-zA-Z0-9](?:[a-zA-Z0-9-]{0,61}[a-zA-Z0-9])?(?:\\.[a-zA-Z0-9](?:[a-zA-Z0-9-]{0,61}[a-zA-Z0-9])?)*$"));
	FRegexMatcher matcher(pattern, Email);

	return matcher.FindNext();
}

bool UStomtAPI::DoesScreenshotFileExist()
{
	return FPaths::FileExists(FPaths::ScreenShotDir() + this->DefaultScreenshotName);
}

void UStomtAPI::UseScreenshotUpload(bool UseUpload)
{
	UseImageUpload = UseUpload;
}

void UStomtAPI::AddCustomKeyValuePair(FString key, FString value)
{
	TArray<FString> pair = TArray<FString>();
	pair.Add(key);
	pair.Add(value);

	CustomKeyValuePairs.Add(pair);
}

bool UStomtAPI::WriteFile(FString TextToSave, FString FileName, FString SaveDirectory, bool AllowOverwriting)
{
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	// CreateDirectoryTree returns true if the destination
	// directory existed prior to call or has been created
	// during the call.
	if (PlatformFile.CreateDirectoryTree(*SaveDirectory))
	{
		// Get absolute file path
		FString AbsoluteFilePath = SaveDirectory + "/" + FileName;

		// Allow overwriting or file doesn't already exist
		if (AllowOverwriting || !FPaths::FileExists(*AbsoluteFilePath))
		{
			//Use " FFileHelper::EEncodingOptions::AutoDetect, &IFileManager::Get(), EFileWrite::FILEWRITE_Append);" for append
			return FFileHelper::SaveStringToFile(TextToSave, *AbsoluteFilePath);
		}
		else
		{
			return false;
		}
	}
	else
	{
		return false;
	}
}

bool UStomtAPI::ReadFile(FString& Result, FString FileName, FString SaveDirectory)
{	

	FString path = SaveDirectory + FileName;

	if (!FPaths::FileExists(path))
	{
		UE_LOG(StomtFileAccess, Warning, TEXT("File with this path does not exist: %s "), *path);

		return false;
	}

	return FFileHelper::LoadFileToString( Result, *path);

}

TArray<uint8> UStomtAPI::ReadBinaryFile(FString FilePath)
{
	TArray<uint8> BufferArray;
	FFileHelper::LoadFileToArray(BufferArray, *FilePath);
	
	return BufferArray;
}

UStomtRestRequest* UStomtAPI::SetupNewPostRequest()
{
	UStomtRestRequest* request = NewObject<UStomtRestRequest>();
	request->OnRequestFail.AddDynamic(this, &UStomtAPI::OnARequestFailed);

	request->SetVerb(ERequestVerb::POST);
	request->SetHeader(TEXT("appid"), this->GetAppID());

	this->AddAccesstokenToRequest(request);

	return request;
}

UStomtRestRequest * UStomtAPI::SetupNewDeleteRequest()
{
	UStomtRestRequest* request = NewObject<UStomtRestRequest>();
	request->OnRequestFail.AddDynamic(this, &UStomtAPI::OnARequestFailed);

	request->SetVerb(ERequestVerb::DEL);
	request->SetHeader(TEXT("appid"), this->GetAppID());

	this->AddAccesstokenToRequest(request);

	return request;
}

void UStomtAPI::AddAccesstokenToRequest(UStomtRestRequest * Request)
{
	if (!this->Config->GetAccessToken().IsEmpty())
	{
		Request->SetHeader(TEXT("accesstoken"), this->Config->GetAccessToken());
		UE_LOG(StomtNetwork, Log , TEXT("AddAccesstoken: %s "), *this->Config->GetAccessToken());
	}
}
