// Fill out your copyright notice in the Description page of Project Settings.

using System;
using System.IO;
using UnrealBuildTool;

public class ProtobufLibrary : ModuleRules
{
	public ProtobufLibrary(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;
		PublicSystemIncludePaths.Add("$(ModuleDir)/include");
        PublicDefinitions.Add("PROTOBUF_ENABLE_DEBUG_LOGGING_MAY_LEAK_PII=0");
        PublicDefinitions.Add("PROTOBUF_BUILTIN_ATOMIC=0");
        if (Target.Platform == UnrealTargetPlatform.Win64)
		{
            // // Add the import library
            // PublicAdditionalLibraries.Add(Path.Combine(ModuleDirectory, "x64", "Release", "ExampleLibrary.lib"));

            // // Delay-load the DLL, so we can load it from the right place first
            // PublicDelayLoadDLLs.Add("ExampleLibrary.dll");

            // // Ensure that the DLL is staged along with the executable
            // RuntimeDependencies.Add("$(PluginDir)/Binaries/ThirdParty/CudaBoidsLibrary/Win64/ExampleLibrary.dll");

            // ʾ��2�����ļ������������.lib�ļ�
            string libFolder = Path.Combine(ModuleDirectory, "lib");
            if (Directory.Exists(libFolder))
            {
                foreach (string libFile in Directory.GetFiles(libFolder, "*.lib"))
                {
                    Console.WriteLine("Loading lib file: {0}", libFile);
                    PublicAdditionalLibraries.Add(libFile);
                }
            }
        }
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			// PublicDelayLoadDLLs.Add(Path.Combine(ModuleDirectory, "Mac", "Release", "libExampleLibrary.dylib"));
			// RuntimeDependencies.Add("$(PluginDir)/Source/ThirdParty/CudaBoidsLibrary/Mac/Release/libExampleLibrary.dylib");
		}
		else if (Target.Platform == UnrealTargetPlatform.Linux)
		{
			// string ExampleSoPath = Path.Combine("$(PluginDir)", "Binaries", "ThirdParty", "CudaBoidsLibrary", "Linux", "x86_64-unknown-linux-gnu", "libExampleLibrary.so");
			// PublicAdditionalLibraries.Add(ExampleSoPath);
			// PublicDelayLoadDLLs.Add(ExampleSoPath);
			// RuntimeDependencies.Add(ExampleSoPath);
		}

        /**
		*��������������������ϲ�����ʱ��UE4��I��Protobuf����Ҫ�����ã�
		*��ʵ��ע��������Щ����Ҳ�ܱ���ͨ��
		*/
        CppCompileWarningSettings.ShadowVariableWarningLevel = WarningLevel.Off;
        //CppCompileWarningSettings.bEnableUndefinedIdentifierWarnings = false;
		bEnableExceptions =  true;
        PublicDefinitions.Add("_CRT_SECURE_NO_WARNINGS");
		PublicDefinitions.Add("GOOGLE_PROTOBUF_NO_RTTI=1");
		PublicDefinitions.Add("GOOGLE_PROTOBUF_CMAKE_BUILD");
	}
}
