// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataLayerSync/StageDataLayerNameParser.h"
#include "Internationalization/Regex.h"

// 前缀常量
const FString FStageDataLayerNameParser::StagePrefix = TEXT("DL_Stage_");
const FString FStageDataLayerNameParser::ActPrefix = TEXT("DL_Act_");

FDataLayerNameParseResult FStageDataLayerNameParser::Parse(const FString& DataLayerName)
{
	FDataLayerNameParseResult Result;

	if (DataLayerName.IsEmpty())
	{
		return Result;
	}

	// Stage Pattern: ^DL_Stage_(.+)$
	// 匹配 DL_Stage_ 后面跟着任意非空字符
	static const FRegexPattern StagePattern(TEXT("^DL_Stage_(.+)$"));
	FRegexMatcher StageMatcher(StagePattern, DataLayerName);

	if (StageMatcher.FindNext())
	{
		Result.bIsValid = true;
		Result.bIsStageLayer = true;
		Result.StageName = StageMatcher.GetCaptureGroup(1);
		return Result;
	}

	// Act Pattern: ^DL_Act_([^_]+)_(.+)$
	// 匹配 DL_Act_ 后面跟着 StageName_ActName
	// 第一个下划线分隔的部分是 StageName，剩余部分是 ActName
	static const FRegexPattern ActPattern(TEXT("^DL_Act_([^_]+)_(.+)$"));
	FRegexMatcher ActMatcher(ActPattern, DataLayerName);

	if (ActMatcher.FindNext())
	{
		Result.bIsValid = true;
		Result.bIsStageLayer = false;
		Result.StageName = ActMatcher.GetCaptureGroup(1);
		Result.ActName = ActMatcher.GetCaptureGroup(2);
		return Result;
	}

	// 不符合任何规范
	Result.bIsValid = false;
	return Result;
}

bool FStageDataLayerNameParser::IsStageDataLayer(const FString& DataLayerName)
{
	if (!DataLayerName.StartsWith(StagePrefix))
	{
		return false;
	}

	// 确保前缀后面有内容
	return DataLayerName.Len() > StagePrefix.Len();
}

bool FStageDataLayerNameParser::IsActDataLayer(const FString& DataLayerName)
{
	if (!DataLayerName.StartsWith(ActPrefix))
	{
		return false;
	}

	// 详细规范：DL_Act_<StageName>_<ActName>，必须包含至少两个下划线
	FString Suffix = DataLayerName.Mid(ActPrefix.Len());
	return Suffix.Contains(TEXT("_")) && Suffix.Len() > 1;
}

bool FStageDataLayerNameParser::IsValidStageEditorDataLayer(const FString& DataLayerName)
{
	return IsStageDataLayer(DataLayerName) || IsActDataLayer(DataLayerName);
}

FString FStageDataLayerNameParser::MakeStageDataLayerName(const FString& StageName)
{
	if (StageName.IsEmpty())
	{
		return FString();
	}

	return StagePrefix + StageName;
}

FString FStageDataLayerNameParser::MakeActDataLayerName(const FString& StageName, const FString& ActName)
{
	if (StageName.IsEmpty() || ActName.IsEmpty())
	{
		return FString();
	}

	return ActPrefix + StageName + TEXT("_") + ActName;
}
