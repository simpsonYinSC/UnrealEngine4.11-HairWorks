// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Text;
using System.IO;

namespace UnrealBuildTool
{
	/// <summary>
	/// Represents a folder within the master project (e.g. Visual Studio solution)
	/// </summary>
	public class MakefileFolder : MasterProjectFolder
	{
		/// <summary>
		/// Constructor
		/// </summary>
		public MakefileFolder( ProjectFileGenerator InitOwnerProjectFileGenerator, string InitFolderName )
			: base(InitOwnerProjectFileGenerator, InitFolderName)
		{
		}
	}

	public class MakefileProjectFile : ProjectFile
	{
		public MakefileProjectFile( FileReference InitFilePath )
			: base(InitFilePath)
		{
		}
	}

	/// <summary>
	/// Makefile project file generator implementation
	/// </summary>
	public class MakefileGenerator : ProjectFileGenerator
	{
		/// True if intellisense data should be generated (takes a while longer)
		/// Now this is needed for project target generation.
		bool bGenerateIntelliSenseData = true;

		/// True if we should include IntelliSense data in the generated project files when possible
		override public bool ShouldGenerateIntelliSenseData()
		{
			return bGenerateIntelliSenseData;
		}

		/// File extension for project files we'll be generating (e.g. ".vcxproj")
		override public string ProjectFileExtension
		{
			get
			{
				return ".mk";
			}
		}

		protected override bool WriteMasterProjectFile( ProjectFile UBTProject )
		{
			bool bSuccess = true;
			return bSuccess;
		}

        private bool WriteMakefile()
        {
            string GameProjectFile = "";
            string BuildCommand = "";
            string ProjectBuildCommand = "";

            string MakeGameProjectFile = "";

            var UnrealRootPath = Path.GetFullPath(ProjectFileGenerator.RootRelativePath);

            if (!String.IsNullOrEmpty(GameProjectName))
            {
                GameProjectFile = UnrealBuildTool.GetUProjectFile().FullName;
                MakeGameProjectFile = "GAMEPROJECTFILE =" + GameProjectFile + "\n";
                ProjectBuildCommand = "PROJECTBUILD = mono $(UNREALROOTPATH)/Engine/Binaries/DotNET/UnrealBuildTool.exe\n";
            }

            BuildCommand = "BUILD = bash $(UNREALROOTPATH)/Engine/Build/BatchFiles/Linux/Build.sh\n";

            var FileName = "Makefile"; // MasterProjectName + ".mk";
            var MakefileContent = new StringBuilder();
            MakefileContent.Append(
                "# Makefile generated by MakefileGenerator.cs\n" +
                "# *DO NOT EDIT*\n\n" +
                "UNREALROOTPATH = \"" + UnrealRootPath + "\"\n" +
                MakeGameProjectFile + "\n" +
                "TARGETS ="
            );
            String MakeProjectCmdArg = "";
            String MakeBuildCommand = "";
            foreach(var Project in GeneratedProjectFiles)
            {
				foreach (var TargetFile in Project.ProjectTargets)
				{
					if (TargetFile.TargetFilePath == null)
					{
						continue;
					}

					string TargetFileName = TargetFile.TargetFilePath.GetFileNameWithoutExtension();
					string Basename = TargetFileName.Substring(0, TargetFileName.LastIndexOf(".Target"));

					foreach (UnrealTargetConfiguration CurConfiguration in Enum.GetValues(typeof(UnrealTargetConfiguration)))
					{
						if (CurConfiguration != UnrealTargetConfiguration.Unknown && CurConfiguration != UnrealTargetConfiguration.Development)
						{
							if (UnrealBuildTool.IsValidConfiguration(CurConfiguration))
							{
								var Confname = Enum.GetName(typeof(UnrealTargetConfiguration), CurConfiguration);
								MakefileContent.Append(String.Format(" \\\n\t{0}-Linux-{1} ", Basename, Confname));
							}
						}
					}
					MakefileContent.Append(" \\\n\t" + Basename);
				}
            }            
            MakefileContent.Append("\\\n\tconfigure");

            MakefileContent.Append("\n\n" + BuildCommand + ProjectBuildCommand + "\n" +
                "all: StandardSet\n\n" +
                "RequiredTools: CrashReportClient ShaderCompileWorker UnrealPak UnrealLightmass\n\n" +
                "StandardSet: RequiredTools UnrealFrontend UE4Editor\n\n" +
                "DebugSet: RequiredTools UnrealFrontend-Linux-Debug UE4Editor-Linux-Debug\n\n"
            );

			foreach(var Project in GeneratedProjectFiles)
			{
				foreach (var TargetFile in Project.ProjectTargets)
				{
					if (TargetFile.TargetFilePath == null)
					{
						continue;
					}

					string TargetFileName = TargetFile.TargetFilePath.GetFileNameWithoutExtension();
					string Basename = TargetFileName.Substring(0, TargetFileName.LastIndexOf(".Target"));

					if (Basename == GameProjectName || Basename == (GameProjectName + "Editor")) 
					{
						MakeProjectCmdArg = " -project=\"\\\"$(GAMEPROJECTFILE)\\\"\"";
						MakeBuildCommand = "$(PROJECTBUILD)";
					}
					else
					{
						MakeBuildCommand = "$(BUILD)";
					}
					
					foreach (UnrealTargetConfiguration CurConfiguration in Enum.GetValues(typeof(UnrealTargetConfiguration)))
					{
						if (Basename == GameProjectName || Basename == (GameProjectName + "Editor")) 
						{
							MakeProjectCmdArg = " -project=\"\\\"$(GAMEPROJECTFILE)\\\"\"";
							MakeBuildCommand = "$(PROJECTBUILD)";
						}
						else
						{
							MakeBuildCommand = "$(BUILD)";
						}

						if (CurConfiguration != UnrealTargetConfiguration.Unknown && CurConfiguration != UnrealTargetConfiguration.Development)
						{
							if (UnrealBuildTool.IsValidConfiguration(CurConfiguration))
							{
								var Confname = Enum.GetName(typeof(UnrealTargetConfiguration), CurConfiguration);
								MakefileContent.Append(String.Format("\n{1}-Linux-{2}:\n\t {0} {1} Linux {2} {3} $(ARGS)\n", MakeBuildCommand, Basename, Confname, MakeProjectCmdArg));
							}
						}
					}
					MakefileContent.Append(String.Format("\n{1}:\n\t {0} {1} Linux Development {2} $(ARGS)\n", MakeBuildCommand, Basename, MakeProjectCmdArg));
				}
			}

            MakefileContent.Append("\nconfigure:\n");
            if (!String.IsNullOrEmpty (GameProjectName))
            {
                // Make sure UBT is updated.
                MakefileContent.Append("\txbuild /property:Configuration=Development /property:TargetFrameworkVersion=v4.0 /verbosity:quiet /nologo ");
                MakefileContent.Append("$(UNREALROOTPATH)/Engine/Source/Programs/UnrealBuildTool/UnrealBuildTool.csproj\n");
                MakefileContent.Append("\t$(PROJECTBUILD) -makefile -qmakefile -cmakefile -project=\"\\\"$(GAMEPROJECTFILE)\\\"\" -game -engine \n");
            }
            else
            {
                MakefileContent.Append("\tbash $(UNREALROOTPATH)/GenerateProjectFiles.sh \n");
            }

            MakefileContent.Append("\n.PHONY: $(TARGETS)\n");
            FileReference FullFileName = FileReference.Combine(MasterProjectPath, FileName);
            return WriteFileIfChanged(FullFileName.FullName, MakefileContent.ToString());
        }

        /// ProjectFileGenerator interface
        //protected override bool WriteMasterProjectFile( ProjectFile UBTProject )
        protected override bool WriteProjectFiles()
        {
			return WriteMakefile();
        }

        /// ProjectFileGenerator interface
        public override MasterProjectFolder AllocateMasterProjectFolder( ProjectFileGenerator InitOwnerProjectFileGenerator, string InitFolderName )
        {
            return new MakefileFolder( InitOwnerProjectFileGenerator, InitFolderName );
        }

        /// ProjectFileGenerator interface
        /// <summary>
        /// Allocates a generator-specific project file object
        /// </summary>
        /// <param name="InitFilePath">Path to the project file</param>
        /// <returns>The newly allocated project file object</returns>
        protected override ProjectFile AllocateProjectFile( FileReference InitFilePath )
        {
            return new MakefileProjectFile( InitFilePath );
        }

        /// ProjectFileGenerator interface
        public override void CleanProjectFiles(DirectoryReference InMasterProjectDirectory, string InMasterProjectName, DirectoryReference InIntermediateProjectFilesDirectory)
        {
        }
    }
}
