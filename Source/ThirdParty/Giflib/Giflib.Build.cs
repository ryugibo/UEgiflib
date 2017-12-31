// Fill out your copyright notice in the Description page of Project Settings.

using System.IO;
using UnrealBuildTool;

public class Giflib : ModuleRules
{
	public Giflib(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		string GiflibDirectory = Path.Combine(ModuleDirectory, "giflib");
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			// Add the import library
			PublicLibraryPaths.Add(Path.Combine(GiflibDirectory, "Release"));
			PublicAdditionalLibraries.Add("giflib.lib");
		}
	}
}
