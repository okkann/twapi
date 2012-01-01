#
# Copyright (c) 2007-2010 Ashok P. Nadkarni
# All rights reserved.
#
# See the file LICENSE for license

# Stuff dealing with Microsoft Installer

namespace eval twapi {
    # Holds the MSI prototypes indexed by name
    variable msiprotos_installer
    variable msiprotos_database
    variable msiprotos_record
    variable msi_guids
    array set msi_guids {
        installer {{000C1090-0000-0000-C000-000000000046}}
        database {{000C109D-0000-0000-C000-000000000046}}
        record {{000C1093-0000-0000-C000-000000000046}}
        summaryinfo {{000C109B-0000-0000-C000-000000000046}}
        stringlist {{000C1095-0000-0000-C000-000000000046}}
        view {{000C109C-0000-0000-C000-000000000046}}
            
    }
}

# Initialize MSI module
proc twapi::init_msi {} {
    # Load all the prototypes and definitions
    # Following code generated using the generate_code_from_typelib function
    # msi objects do not support ITypeInfo so cannot use at run time binding

    # Dispatch Interface Installer
    # Installer Methods
    ::twapi::_dispatch_prototype_set {{000C1090-0000-0000-C000-000000000046}} CreateRecord 1033 1 {1 1033 1 {26 {29 200}} {{3 1}} Count}
    ::twapi::_dispatch_prototype_set {{000C1090-0000-0000-C000-000000000046}} OpenPackage 1033 1 {2 1033 1 {26 {29 400}} {{12 1} {3 {49 {3 0}}}} {PackagePath Options}}
    ::twapi::_dispatch_prototype_set {{000C1090-0000-0000-C000-000000000046}} OpenProduct 1033 1 {3 1033 1 {26 {29 400}} {{8 1}} ProductCode}
    ::twapi::_dispatch_prototype_set {{000C1090-0000-0000-C000-000000000046}} OpenDatabase 1033 1 {4 1033 1 {26 {29 600}} {{8 1} {12 1}} {DatabasePath OpenMode}}
    ::twapi::_dispatch_prototype_set {{000C1090-0000-0000-C000-000000000046}} SummaryInformation 1033 2 {5 1033 2 {26 {29 800}} {{8 1} {3 {49 {3 0}}}} {PackagePath UpdateCount}}
    ::twapi::_dispatch_prototype_set {{000C1090-0000-0000-C000-000000000046}} EnableLog 1033 1 {7 1033 1 24 {{8 1} {8 1}} {LogMode LogFile}}
    ::twapi::_dispatch_prototype_set {{000C1090-0000-0000-C000-000000000046}} InstallProduct 1033 1 {8 1033 1 24 {{8 1} {8 {49 {8 0}}}} {PackagePath PropertyValues}}
    ::twapi::_dispatch_prototype_set {{000C1090-0000-0000-C000-000000000046}} Version 1033 2 {9 1033 2 8 {} {}}
    ::twapi::_dispatch_prototype_set {{000C1090-0000-0000-C000-000000000046}} LastErrorRecord 1033 1 {10 1033 1 {26 {29 200}} {} {}}
    ::twapi::_dispatch_prototype_set {{000C1090-0000-0000-C000-000000000046}} RegistryValue 1033 1 {11 1033 1 8 {{12 1} {8 1} {12 17}} {Root Key Value}}
    ::twapi::_dispatch_prototype_set {{000C1090-0000-0000-C000-000000000046}} FileAttributes 1033 1 {13 1033 1 3 {{8 1}} FilePath}
    ::twapi::_dispatch_prototype_set {{000C1090-0000-0000-C000-000000000046}} FileSize 1033 1 {15 1033 1 3 {{8 1}} FilePath}
    ::twapi::_dispatch_prototype_set {{000C1090-0000-0000-C000-000000000046}} FileVersion 1033 1 {16 1033 1 8 {{8 1} {12 17}} {FilePath Language}}
    ::twapi::_dispatch_prototype_set {{000C1090-0000-0000-C000-000000000046}} Environment 1033 2 {12 1033 2 8 {{8 1}} Variable}
    ::twapi::_dispatch_prototype_set {{000C1090-0000-0000-C000-000000000046}} Environment 1033 4 {12 1033 4 24 {{8 1} {8 1}} Variable}
    ::twapi::_dispatch_prototype_set {{000C1090-0000-0000-C000-000000000046}} ProductState 1033 2 {17 1033 2 {29 1900} {{8 1}} Product}
    ::twapi::_dispatch_prototype_set {{000C1090-0000-0000-C000-000000000046}} ProductInfo 1033 2 {18 1033 2 8 {{8 1} {8 1}} {Product Attribute}}
    ::twapi::_dispatch_prototype_set {{000C1090-0000-0000-C000-000000000046}} ConfigureProduct 1033 1 {19 1033 1 24 {{8 1} {3 1} {3 1}} {Product InstallLevel InstallState}}
    ::twapi::_dispatch_prototype_set {{000C1090-0000-0000-C000-000000000046}} ReinstallProduct 1033 1 {20 1033 1 24 {{8 1} {3 1}} {Product ReinstallMode}}
    ::twapi::_dispatch_prototype_set {{000C1090-0000-0000-C000-000000000046}} CollectUserInfo 1033 1 {21 1033 1 24 {{8 1}} Product}
    ::twapi::_dispatch_prototype_set {{000C1090-0000-0000-C000-000000000046}} ApplyPatch 1033 1 {22 1033 1 24 {{8 1} {8 1} {3 1} {8 1}} {PatchPackage InstallPackage InstallType CommandLine}}
    ::twapi::_dispatch_prototype_set {{000C1090-0000-0000-C000-000000000046}} FeatureParent 1033 2 {23 1033 2 8 {{8 1} {8 1}} {Product Feature}}
    ::twapi::_dispatch_prototype_set {{000C1090-0000-0000-C000-000000000046}} FeatureState 1033 2 {24 1033 2 {29 1900} {{8 1} {8 1}} {Product Feature}}
    ::twapi::_dispatch_prototype_set {{000C1090-0000-0000-C000-000000000046}} UseFeature 1033 1 {25 1033 1 24 {{8 1} {8 1} {3 1}} {Product Feature InstallMode}}
    ::twapi::_dispatch_prototype_set {{000C1090-0000-0000-C000-000000000046}} FeatureUsageCount 1033 2 {26 1033 2 3 {{8 1} {8 1}} {Product Feature}}
    ::twapi::_dispatch_prototype_set {{000C1090-0000-0000-C000-000000000046}} FeatureUsageDate 1033 2 {27 1033 2 7 {{8 1} {8 1}} {Product Feature}}
    ::twapi::_dispatch_prototype_set {{000C1090-0000-0000-C000-000000000046}} ConfigureFeature 1033 1 {28 1033 1 24 {{8 1} {8 1} {3 1}} {Product Feature InstallState}}
    ::twapi::_dispatch_prototype_set {{000C1090-0000-0000-C000-000000000046}} ReinstallFeature 1033 1 {29 1033 1 24 {{8 1} {8 1} {3 1}} {Product Feature ReinstallMode}}
    ::twapi::_dispatch_prototype_set {{000C1090-0000-0000-C000-000000000046}} ProvideComponent 1033 1 {30 1033 1 8 {{8 1} {8 1} {8 1} {3 1}} {Product Feature Component InstallMode}}
    ::twapi::_dispatch_prototype_set {{000C1090-0000-0000-C000-000000000046}} ComponentPath 1033 2 {31 1033 2 8 {{8 1} {8 1}} {Product Component}}
    ::twapi::_dispatch_prototype_set {{000C1090-0000-0000-C000-000000000046}} ProvideQualifiedComponent 1033 1 {32 1033 1 8 {{8 1} {8 1} {3 1}} {Category Qualifier InstallMode}}
    ::twapi::_dispatch_prototype_set {{000C1090-0000-0000-C000-000000000046}} QualifierDescription 1033 2 {33 1033 2 8 {{8 1} {8 1}} {Category Qualifier}}
    ::twapi::_dispatch_prototype_set {{000C1090-0000-0000-C000-000000000046}} ComponentQualifiers 1033 2 {34 1033 2 {26 {29 2600}} {{8 1}} Category}
    ::twapi::_dispatch_prototype_set {{000C1090-0000-0000-C000-000000000046}} Products 1033 2 {35 1033 2 {26 {29 2600}} {} {}}
    ::twapi::_dispatch_prototype_set {{000C1090-0000-0000-C000-000000000046}} Features 1033 2 {36 1033 2 {26 {29 2600}} {{8 1}} Product}
    ::twapi::_dispatch_prototype_set {{000C1090-0000-0000-C000-000000000046}} Components 1033 2 {37 1033 2 {26 {29 2600}} {} {}}
    ::twapi::_dispatch_prototype_set {{000C1090-0000-0000-C000-000000000046}} ComponentClients 1033 2 {38 1033 2 {26 {29 2600}} {{8 1}} Component}
    ::twapi::_dispatch_prototype_set {{000C1090-0000-0000-C000-000000000046}} Patches 1033 2 {39 1033 2 {26 {29 2600}} {{8 1}} Product}
    ::twapi::_dispatch_prototype_set {{000C1090-0000-0000-C000-000000000046}} RelatedProducts 1033 2 {40 1033 2 {26 {29 2600}} {{8 1}} UpgradeCode}
    ::twapi::_dispatch_prototype_set {{000C1090-0000-0000-C000-000000000046}} PatchInfo 1033 2 {41 1033 2 8 {{8 1} {8 1}} {Patch Attribute}}
    ::twapi::_dispatch_prototype_set {{000C1090-0000-0000-C000-000000000046}} PatchTransforms 1033 2 {42 1033 2 8 {{8 1} {8 1}} {Product Patch}}
    ::twapi::_dispatch_prototype_set {{000C1090-0000-0000-C000-000000000046}} AddSource 1033 1 {43 1033 1 24 {{8 1} {8 1} {8 1}} {Product User Source}}
    ::twapi::_dispatch_prototype_set {{000C1090-0000-0000-C000-000000000046}} ClearSourceList 1033 1 {44 1033 1 24 {{8 1} {8 1}} {Product User}}
    ::twapi::_dispatch_prototype_set {{000C1090-0000-0000-C000-000000000046}} ForceSourceListResolution 1033 1 {45 1033 1 24 {{8 1} {8 1}} {Product User}}
    ::twapi::_dispatch_prototype_set {{000C1090-0000-0000-C000-000000000046}} GetShortcutTarget 1033 2 {46 1033 2 {26 {29 200}} {{8 1}} ShortcutPath}
    ::twapi::_dispatch_prototype_set {{000C1090-0000-0000-C000-000000000046}} FileHash 1033 1 {47 1033 1 {26 {29 200}} {{8 1} {3 1}} {FilePath Options}}
    ::twapi::_dispatch_prototype_set {{000C1090-0000-0000-C000-000000000046}} FileSignatureInfo 1033 1 {48 1033 1 {27 17} {{8 1} {3 1} {3 1}} {FilePath Options Format}}
    ::twapi::_dispatch_prototype_set {{000C1090-0000-0000-C000-000000000046}} RemovePatches 1033 1 {49 1033 1 24 {{8 1} {8 1} {3 1} {8 {49 {8 0}}}} {PatchList Product UninstallType PropertyList}}
    ::twapi::_dispatch_prototype_set {{000C1090-0000-0000-C000-000000000046}} ApplyMultiplePatches 1033 1 {51 1033 1 24 {{8 1} {8 1} {8 1}} {PatchPackage Product PropertiesList}}
    ::twapi::_dispatch_prototype_set {{000C1090-0000-0000-C000-000000000046}} Product 1033 2 {53 1033 2 25 {{8 1} {8 1} {3 1} {{26 {26 {29 2900}}} 10}} {Product UserSid iContext retval}}
    ::twapi::_dispatch_prototype_set {{000C1090-0000-0000-C000-000000000046}} Patch 1033 2 {56 1033 2 25 {{8 1} {8 1} {8 1} {3 1} {{26 {26 {29 3100}}} 10}} {PatchCode ProductCode UserSid iContext retval}}
    ::twapi::_dispatch_prototype_set {{000C1090-0000-0000-C000-000000000046}} ProductsEx 1033 2 {52 1033 2 {26 {29 2200}} {{8 1} {8 1} {3 1}} {Product UserSid Contexts}}
    ::twapi::_dispatch_prototype_set {{000C1090-0000-0000-C000-000000000046}} PatchesEx 1033 2 {55 1033 2 {26 {29 2200}} {{8 1} {8 1} {3 1} {3 1}} {Product UserSid Contexts filter}}
    ::twapi::_dispatch_prototype_set {{000C1090-0000-0000-C000-000000000046}} ExtractPatchXMLData 1033 1 {57 1033 1 8 {{8 1}} PatchPath}
    # Installer Properties
    ::twapi::_dispatch_prototype_set {{000C1090-0000-0000-C000-000000000046}} UILevel 1033 2 {6 1033 2 {29 100} {} {}}
    ::twapi::_dispatch_prototype_set {{000C1090-0000-0000-C000-000000000046}} UILevel 1033 4 {6 1033 4 24 {{{29 100} 1}} {}}

    # Dispatch Interface Record
    # Record Methods
    ::twapi::_dispatch_prototype_set {{000C1093-0000-0000-C000-000000000046}} StringData 1033 2 {1 1033 2 8 {{3 1}} Field}
    ::twapi::_dispatch_prototype_set {{000C1093-0000-0000-C000-000000000046}} StringData 1033 4 {1 1033 4 24 {{3 1} {8 1}} Field}
    ::twapi::_dispatch_prototype_set {{000C1093-0000-0000-C000-000000000046}} IntegerData 1033 2 {2 1033 2 3 {{3 1}} Field}
    ::twapi::_dispatch_prototype_set {{000C1093-0000-0000-C000-000000000046}} IntegerData 1033 4 {2 1033 4 24 {{3 1} {3 1}} Field}
    ::twapi::_dispatch_prototype_set {{000C1093-0000-0000-C000-000000000046}} SetStream 1033 1 {3 1033 1 24 {{3 1} {8 1}} {Field FilePath}}
    ::twapi::_dispatch_prototype_set {{000C1093-0000-0000-C000-000000000046}} ReadStream 1033 1 {4 1033 1 8 {{3 1} {3 1} {3 1}} {Field Length Format}}
    ::twapi::_dispatch_prototype_set {{000C1093-0000-0000-C000-000000000046}} FieldCount 1033 2 {0 1033 2 3 {} {}}
    ::twapi::_dispatch_prototype_set {{000C1093-0000-0000-C000-000000000046}} IsNull 1033 2 {6 1033 2 11 {{3 1}} Field}
    ::twapi::_dispatch_prototype_set {{000C1093-0000-0000-C000-000000000046}} DataSize 1033 2 {5 1033 2 3 {{3 1}} Field}
    ::twapi::_dispatch_prototype_set {{000C1093-0000-0000-C000-000000000046}} ClearData 1033 1 {7 1033 1 24 {} {}}
    ::twapi::_dispatch_prototype_set {{000C1093-0000-0000-C000-000000000046}} FormatText 1033 1 {8 1033 1 8 {} {}}

    # Dispatch Interface Session
    # Session Methods
    ::twapi::_dispatch_prototype_set {{000C109E-0000-0000-C000-000000000046}} Installer 1033 2 {1 1033 2 {26 {29 0}} {} {}}
    ::twapi::_dispatch_prototype_set {{000C109E-0000-0000-C000-000000000046}} Property 1033 2 {2 1033 2 8 {{8 1}} Name}
    ::twapi::_dispatch_prototype_set {{000C109E-0000-0000-C000-000000000046}} Property 1033 4 {2 1033 4 24 {{8 1} {8 1}} Name}
    ::twapi::_dispatch_prototype_set {{000C109E-0000-0000-C000-000000000046}} Language 1033 2 {3 1033 2 3 {} {}}
    ::twapi::_dispatch_prototype_set {{000C109E-0000-0000-C000-000000000046}} Mode 1033 2 {4 1033 2 11 {{3 1}} Flag}
    ::twapi::_dispatch_prototype_set {{000C109E-0000-0000-C000-000000000046}} Mode 1033 4 {4 1033 4 24 {{3 1} {11 1}} Flag}
    ::twapi::_dispatch_prototype_set {{000C109E-0000-0000-C000-000000000046}} Database 1033 2 {5 1033 2 {26 {29 600}} {} {}}
    ::twapi::_dispatch_prototype_set {{000C109E-0000-0000-C000-000000000046}} SourcePath 1033 2 {6 1033 2 8 {{8 1}} Folder}
    ::twapi::_dispatch_prototype_set {{000C109E-0000-0000-C000-000000000046}} TargetPath 1033 2 {7 1033 2 8 {{8 1}} Folder}
    ::twapi::_dispatch_prototype_set {{000C109E-0000-0000-C000-000000000046}} TargetPath 1033 4 {7 1033 4 24 {{8 1} {8 1}} Folder}
    ::twapi::_dispatch_prototype_set {{000C109E-0000-0000-C000-000000000046}} DoAction 1033 1 {8 1033 1 {29 1600} {{8 1}} Action}
    ::twapi::_dispatch_prototype_set {{000C109E-0000-0000-C000-000000000046}} Sequence 1033 1 {9 1033 1 {29 1600} {{8 1} {12 17}} {Table Mode}}
    ::twapi::_dispatch_prototype_set {{000C109E-0000-0000-C000-000000000046}} EvaluateCondition 1033 1 {10 1033 1 {29 1400} {{8 1}} Expression}
    ::twapi::_dispatch_prototype_set {{000C109E-0000-0000-C000-000000000046}} FormatRecord 1033 1 {11 1033 1 8 {{9 1}} Record}
    ::twapi::_dispatch_prototype_set {{000C109E-0000-0000-C000-000000000046}} Message 1033 1 {12 1033 1 {29 1700} {{3 1} {9 1}} {Kind Record}}
    ::twapi::_dispatch_prototype_set {{000C109E-0000-0000-C000-000000000046}} FeatureCurrentState 1033 2 {13 1033 2 {29 1900} {{8 1}} Feature}
    ::twapi::_dispatch_prototype_set {{000C109E-0000-0000-C000-000000000046}} FeatureRequestState 1033 2 {14 1033 2 {29 1900} {{8 1}} Feature}
    ::twapi::_dispatch_prototype_set {{000C109E-0000-0000-C000-000000000046}} FeatureRequestState 1033 4 {14 1033 4 24 {{8 1} {3 1}} Feature}
    ::twapi::_dispatch_prototype_set {{000C109E-0000-0000-C000-000000000046}} FeatureValidStates 1033 2 {15 1033 2 3 {{8 1}} Feature}
    ::twapi::_dispatch_prototype_set {{000C109E-0000-0000-C000-000000000046}} FeatureCost 1033 2 {16 1033 2 3 {{8 1} {3 1} {3 1}} {Feature CostTree State}}
    ::twapi::_dispatch_prototype_set {{000C109E-0000-0000-C000-000000000046}} ComponentCurrentState 1033 2 {17 1033 2 {29 1900} {{8 1}} Component}
    ::twapi::_dispatch_prototype_set {{000C109E-0000-0000-C000-000000000046}} ComponentRequestState 1033 2 {18 1033 2 {29 1900} {{8 1}} Component}
    ::twapi::_dispatch_prototype_set {{000C109E-0000-0000-C000-000000000046}} ComponentRequestState 1033 4 {18 1033 4 24 {{8 1} {3 1}} Component}
    ::twapi::_dispatch_prototype_set {{000C109E-0000-0000-C000-000000000046}} SetInstallLevel 1033 1 {19 1033 1 24 {{3 1}} Level}
    ::twapi::_dispatch_prototype_set {{000C109E-0000-0000-C000-000000000046}} VerifyDiskSpace 1033 2 {20 1033 2 11 {} {}}
    ::twapi::_dispatch_prototype_set {{000C109E-0000-0000-C000-000000000046}} ProductProperty 1033 2 {21 1033 2 8 {{8 1}} Property}
    ::twapi::_dispatch_prototype_set {{000C109E-0000-0000-C000-000000000046}} FeatureInfo 1033 2 {22 1033 2 {26 {29 2100}} {{8 1}} Feature}
    ::twapi::_dispatch_prototype_set {{000C109E-0000-0000-C000-000000000046}} ComponentCosts 1033 2 {23 1033 2 {26 {29 2200}} {{8 1} {3 1}} {Component State}}

    # Dispatch Interface Database
    # Database Methods
    ::twapi::_dispatch_prototype_set {{000C109D-0000-0000-C000-000000000046}} DatabaseState 1033 2 {1 1033 2 {29 700} {} {}}
    ::twapi::_dispatch_prototype_set {{000C109D-0000-0000-C000-000000000046}} SummaryInformation 1033 2 {2 1033 2 {26 {29 800}} {{3 {49 {3 0}}}} UpdateCount}
    ::twapi::_dispatch_prototype_set {{000C109D-0000-0000-C000-000000000046}} OpenView 1033 1 {3 1033 1 {26 {29 900}} {{8 1}} Sql}
    ::twapi::_dispatch_prototype_set {{000C109D-0000-0000-C000-000000000046}} Commit 1033 1 {4 1033 1 24 {} {}}
    ::twapi::_dispatch_prototype_set {{000C109D-0000-0000-C000-000000000046}} PrimaryKeys 1033 2 {5 1033 2 {26 {29 200}} {{8 1}} Table}
    ::twapi::_dispatch_prototype_set {{000C109D-0000-0000-C000-000000000046}} Import 1033 1 {6 1033 1 24 {{8 1} {8 1}} {Folder File}}
    ::twapi::_dispatch_prototype_set {{000C109D-0000-0000-C000-000000000046}} Export 1033 1 {7 1033 1 24 {{8 1} {8 1} {8 1}} {Table Folder File}}
    ::twapi::_dispatch_prototype_set {{000C109D-0000-0000-C000-000000000046}} Merge 1033 1 {8 1033 1 11 {{9 1} {8 {49 {8 0}}}} {Database ErrorTable}}
    ::twapi::_dispatch_prototype_set {{000C109D-0000-0000-C000-000000000046}} GenerateTransform 1033 1 {9 1033 1 11 {{9 1} {8 {49 {8 0}}}} {ReferenceDatabase TransformFile}}
    ::twapi::_dispatch_prototype_set {{000C109D-0000-0000-C000-000000000046}} ApplyTransform 1033 1 {10 1033 1 24 {{8 1} {3 1}} {TransformFile ErrorConditions}}
    ::twapi::_dispatch_prototype_set {{000C109D-0000-0000-C000-000000000046}} EnableUIPreview 1033 1 {11 1033 1 {26 {29 1300}} {} {}}
    ::twapi::_dispatch_prototype_set {{000C109D-0000-0000-C000-000000000046}} TablePersistent 1033 2 {12 1033 2 {29 1400} {{8 1}} Table}
    ::twapi::_dispatch_prototype_set {{000C109D-0000-0000-C000-000000000046}} CreateTransformSummaryInfo 1033 1 {13 1033 1 24 {{9 1} {8 1} {3 1} {3 1}} {ReferenceDatabase TransformFile ErrorConditions Validation}}

    # Dispatch Interface SummaryInfo
    # SummaryInfo Methods
    ::twapi::_dispatch_prototype_set {{000C109B-0000-0000-C000-000000000046}} Property 1033 2 {1 1033 2 12 {{3 1}} Pid}
    ::twapi::_dispatch_prototype_set {{000C109B-0000-0000-C000-000000000046}} Property 1033 4 {1 1033 4 24 {{3 1} {12 1}} Pid}
    ::twapi::_dispatch_prototype_set {{000C109B-0000-0000-C000-000000000046}} PropertyCount 1033 2 {2 1033 2 3 {} {}}
    ::twapi::_dispatch_prototype_set {{000C109B-0000-0000-C000-000000000046}} Persist 1033 1 {3 1033 1 24 {} {}}

    # Dispatch Interface View
    # View Methods
    ::twapi::_dispatch_prototype_set {{000C109C-0000-0000-C000-000000000046}} Execute 1033 1 {1 1033 1 24 {{9 {49 {3 0}}}} Params}
    ::twapi::_dispatch_prototype_set {{000C109C-0000-0000-C000-000000000046}} Fetch 1033 1 {2 1033 1 {26 {29 200}} {} {}}
    ::twapi::_dispatch_prototype_set {{000C109C-0000-0000-C000-000000000046}} Modify 1033 1 {3 1033 1 24 {{3 1} {9 0}} {Mode Record}}
    ::twapi::_dispatch_prototype_set {{000C109C-0000-0000-C000-000000000046}} ColumnInfo 1033 2 {5 1033 2 {26 {29 200}} {{3 1}} Info}
    ::twapi::_dispatch_prototype_set {{000C109C-0000-0000-C000-000000000046}} Close 1033 1 {4 1033 1 24 {} {}}
    ::twapi::_dispatch_prototype_set {{000C109C-0000-0000-C000-000000000046}} GetError 1033 1 {6 1033 1 8 {} {}}

    # Dispatch Interface UIPreview
    # UIPreview Methods
    ::twapi::_dispatch_prototype_set {{000C109A-0000-0000-C000-000000000046}} Property 1033 2 {1 1033 2 8 {{8 1}} Name}
    ::twapi::_dispatch_prototype_set {{000C109A-0000-0000-C000-000000000046}} Property 1033 4 {1 1033 4 24 {{8 1} {8 1}} Name}
    ::twapi::_dispatch_prototype_set {{000C109A-0000-0000-C000-000000000046}} ViewDialog 1033 1 {2 1033 1 24 {{8 1}} Dialog}
    ::twapi::_dispatch_prototype_set {{000C109A-0000-0000-C000-000000000046}} ViewBillboard 1033 1 {3 1033 1 24 {{8 1} {8 1}} {Control Billboard}}

    # Dispatch Interface FeatureInfo
    # FeatureInfo Methods
    ::twapi::_dispatch_prototype_set {{000C109F-0000-0000-C000-000000000046}} Title 1033 2 {1 1033 2 8 {} {}}
    ::twapi::_dispatch_prototype_set {{000C109F-0000-0000-C000-000000000046}} Description 1033 2 {2 1033 2 8 {} {}}
    # FeatureInfo Properties
    ::twapi::_dispatch_prototype_set {{000C109F-0000-0000-C000-000000000046}} Attributes 1033 2 {3 1033 2 3 {} {}}
    ::twapi::_dispatch_prototype_set {{000C109F-0000-0000-C000-000000000046}} Attributes 1033 4 {3 1033 4 24 {{3 1}} {}}

    # Dispatch Interface RecordList
    # RecordList Methods
    ::twapi::_dispatch_prototype_set {{000C1096-0000-0000-C000-000000000046}} _NewEnum 1033 1 {-4 1033 1 13 {} {}}
    ::twapi::_dispatch_prototype_set {{000C1096-0000-0000-C000-000000000046}} Item 1033 2 {0 1033 2 {26 {29 200}} {{3 0}} Index}
    ::twapi::_dispatch_prototype_set {{000C1096-0000-0000-C000-000000000046}} Count 1033 2 {1 1033 2 3 {} {}}

    # Dispatch Interface StringList
    # StringList Methods
    ::twapi::_dispatch_prototype_set {{000C1095-0000-0000-C000-000000000046}} _NewEnum 1033 1 {-4 1033 1 13 {} {}}
    ::twapi::_dispatch_prototype_set {{000C1095-0000-0000-C000-000000000046}} Item 1033 2 {0 1033 2 8 {{3 0}} Index}
    ::twapi::_dispatch_prototype_set {{000C1095-0000-0000-C000-000000000046}} Count 1033 2 {1 1033 2 3 {} {}}

    # Dispatch Interface Product
    # Product Methods
    ::twapi::_dispatch_prototype_set {{000C10A0-0000-0000-C000-000000000046}} ProductCode 1033 2 {1 1033 2 25 {{{26 8} 10}} retval}
    ::twapi::_dispatch_prototype_set {{000C10A0-0000-0000-C000-000000000046}} UserSid 1033 2 {2 1033 2 25 {{{26 8} 10}} retval}
    ::twapi::_dispatch_prototype_set {{000C10A0-0000-0000-C000-000000000046}} Context 1033 2 {3 1033 2 25 {{{26 {29 2800}} 10}} retval}
    ::twapi::_dispatch_prototype_set {{000C10A0-0000-0000-C000-000000000046}} State 1033 2 {4 1033 2 25 {{{26 {29 1900}} 10}} retval}
    ::twapi::_dispatch_prototype_set {{000C10A0-0000-0000-C000-000000000046}} InstallProperty 1033 2 {5 1033 2 25 {{8 1} {{26 8} 10}} {Name retval}}
    ::twapi::_dispatch_prototype_set {{000C10A0-0000-0000-C000-000000000046}} ComponentState 1033 2 {6 1033 2 25 {{8 1} {{26 {29 1900}} 10}} {Component retval}}
    ::twapi::_dispatch_prototype_set {{000C10A0-0000-0000-C000-000000000046}} FeatureState 1033 2 {7 1033 2 25 {{8 1} {{26 {29 1900}} 10}} {Feature retval}}
    ::twapi::_dispatch_prototype_set {{000C10A0-0000-0000-C000-000000000046}} Sources 1033 2 {14 1033 2 25 {{3 1} {{26 {26 {29 2600}}} 10}} {SourceType retval}}
    ::twapi::_dispatch_prototype_set {{000C10A0-0000-0000-C000-000000000046}} MediaDisks 1033 2 {15 1033 2 25 {{{26 {26 {29 2200}}} 10}} retval}
    ::twapi::_dispatch_prototype_set {{000C10A0-0000-0000-C000-000000000046}} SourceListAddSource 1033 1 {8 1033 1 25 {{3 1} {8 1} {3 1}} {iSourceType Source dwIndex}}
    ::twapi::_dispatch_prototype_set {{000C10A0-0000-0000-C000-000000000046}} SourceListAddMediaDisk 1033 1 {9 1033 1 25 {{3 1} {8 1} {8 1}} {dwDiskId VolumeLabel DiskPrompt}}
    ::twapi::_dispatch_prototype_set {{000C10A0-0000-0000-C000-000000000046}} SourceListClearSource 1033 1 {10 1033 1 25 {{3 1} {8 1}} {iSourceType Source}}
    ::twapi::_dispatch_prototype_set {{000C10A0-0000-0000-C000-000000000046}} SourceListClearMediaDisk 1033 1 {11 1033 1 25 {{3 1}} iDiskId}
    ::twapi::_dispatch_prototype_set {{000C10A0-0000-0000-C000-000000000046}} SourceListClearAll 1033 1 {12 1033 1 25 {{3 1}} iSourceType}
    ::twapi::_dispatch_prototype_set {{000C10A0-0000-0000-C000-000000000046}} SourceListForceResolution 1033 1 {13 1033 1 25 {} {}}
    ::twapi::_dispatch_prototype_set {{000C10A0-0000-0000-C000-000000000046}} SourceListInfo 1033 2 {16 1033 2 25 {{8 1} {{26 8} 10}} {Property retval}}
    ::twapi::_dispatch_prototype_set {{000C10A0-0000-0000-C000-000000000046}} SourceListInfo 1033 4 {16 1033 4 25 {{8 1} {8 1}} {Property retval}}

    # Dispatch Interface Patch
    # Patch Methods
    ::twapi::_dispatch_prototype_set {{000C10A1-0000-0000-C000-000000000046}} PatchCode 1033 2 {1 1033 2 25 {{{26 8} 10}} retval}
    ::twapi::_dispatch_prototype_set {{000C10A1-0000-0000-C000-000000000046}} ProductCode 1033 2 {2 1033 2 25 {{{26 8} 10}} retval}
    ::twapi::_dispatch_prototype_set {{000C10A1-0000-0000-C000-000000000046}} UserSid 1033 2 {3 1033 2 25 {{{26 8} 10}} retval}
    ::twapi::_dispatch_prototype_set {{000C10A1-0000-0000-C000-000000000046}} Context 1033 2 {4 1033 2 25 {{{26 {29 2800}} 10}} retval}
    ::twapi::_dispatch_prototype_set {{000C10A1-0000-0000-C000-000000000046}} State 1033 2 {5 1033 2 25 {{{26 {29 1900}} 10}} retval}
    ::twapi::_dispatch_prototype_set {{000C10A1-0000-0000-C000-000000000046}} Sources 1033 2 {12 1033 2 25 {{3 1} {{26 {26 {29 2600}}} 10}} {SourceType retval}}
    ::twapi::_dispatch_prototype_set {{000C10A1-0000-0000-C000-000000000046}} MediaDisks 1033 2 {13 1033 2 25 {{{26 {26 {29 2200}}} 10}} retval}
    ::twapi::_dispatch_prototype_set {{000C10A1-0000-0000-C000-000000000046}} SourceListAddSource 1033 1 {6 1033 1 25 {{3 1} {8 1} {3 1}} {iSourceType Source dwIndex}}
    ::twapi::_dispatch_prototype_set {{000C10A1-0000-0000-C000-000000000046}} SourceListAddMediaDisk 1033 1 {7 1033 1 25 {{3 1} {8 1} {8 1}} {dwDiskId VolumeLabel DiskPrompt}}
    ::twapi::_dispatch_prototype_set {{000C10A1-0000-0000-C000-000000000046}} SourceListClearSource 1033 1 {8 1033 1 25 {{3 1} {8 1}} {iSourceType Source}}
    ::twapi::_dispatch_prototype_set {{000C10A1-0000-0000-C000-000000000046}} SourceListClearMediaDisk 1033 1 {9 1033 1 25 {{3 1}} iDiskId}
    ::twapi::_dispatch_prototype_set {{000C10A1-0000-0000-C000-000000000046}} SourceListClearAll 1033 1 {10 1033 1 25 {{3 1}} iSourceType}
    ::twapi::_dispatch_prototype_set {{000C10A1-0000-0000-C000-000000000046}} SourceListForceResolution 1033 1 {11 1033 1 25 {} {}}
    ::twapi::_dispatch_prototype_set {{000C10A1-0000-0000-C000-000000000046}} SourceListInfo 1033 2 {14 1033 2 25 {{8 1} {{26 8} 10}} {Property retval}}
    ::twapi::_dispatch_prototype_set {{000C10A1-0000-0000-C000-000000000046}} SourceListInfo 1033 4 {14 1033 4 25 {{8 1} {8 1}} {Property retval}}
    ::twapi::_dispatch_prototype_set {{000C10A1-0000-0000-C000-000000000046}} PatchProperty 1033 2 {15 1033 2 25 {{8 1} {{26 8} 10}} {Property Value}}

    # Enum MsiUILevel
    array set MsiUILevel {msiUILevelNoChange 0 msiUILevelDefault 1 msiUILevelNone 2 msiUILevelBasic 3 msiUILevelReduced 4 msiUILevelFull 5 msiUILevelHideCancel 32 msiUILevelProgressOnly 64 msiUILevelEndDialog 128 msiUILevelSourceResOnly 256}

    # Enum MsiReadStream
    array set MsiReadStream {msiReadStreamInteger 0 msiReadStreamBytes 1 msiReadStreamAnsi 2 msiReadStreamDirect 3}

    # Enum MsiRunMode
    array set MsiRunMode {msiRunModeAdmin 0 msiRunModeAdvertise 1 msiRunModeMaintenance 2 msiRunModeRollbackEnabled 3 msiRunModeLogEnabled 4 msiRunModeOperations 5 msiRunModeRebootAtEnd 6 msiRunModeRebootNow 7 msiRunModeCabinet 8 msiRunModeSourceShortNames 9 msiRunModeTargetShortNames 10 msiRunModeWindows9x 12 msiRunModeZawEnabled 13 msiRunModeScheduled 16 msiRunModeRollback 17 msiRunModeCommit 18}

    # Enum MsiDatabaseState
    array set MsiDatabaseState {msiDatabaseStateRead 0 msiDatabaseStateWrite 1}

    # Enum MsiViewModify
    array set MsiViewModify {msiViewModifySeek -1 msiViewModifyRefresh 0 msiViewModifyInsert 1 msiViewModifyUpdate 2 msiViewModifyAssign 3 msiViewModifyReplace 4 msiViewModifyMerge 5 msiViewModifyDelete 6 msiViewModifyInsertTemporary 7 msiViewModifyValidate 8 msiViewModifyValidateNew 9 msiViewModifyValidateField 10 msiViewModifyValidateDelete 11}

    # Enum MsiColumnInfo
    array set MsiColumnInfo {msiColumnInfoNames 0 msiColumnInfoTypes 1}

    # Enum MsiTransformError
    array set MsiTransformError {msiTransformErrorNone 0 msiTransformErrorAddExistingRow 1 msiTransformErrorDeleteNonExistingRow 2 msiTransformErrorAddExistingTable 4 msiTransformErrorDeleteNonExistingTable 8 msiTransformErrorUpdateNonExistingRow 16 msiTransformErrorChangeCodePage 32 msiTransformErrorViewTransform 256}

    # Enum MsiEvaluateCondition
    array set MsiEvaluateCondition {msiEvaluateConditionFalse 0 msiEvaluateConditionTrue 1 msiEvaluateConditionNone 2 msiEvaluateConditionError 3}

    # Enum MsiTransformValidation
    array set MsiTransformValidation {msiTransformValidationNone 0 msiTransformValidationLanguage 1 msiTransformValidationProduct 2 msiTransformValidationPlatform 4 msiTransformValidationMajorVer 8 msiTransformValidationMinorVer 16 msiTransformValidationUpdateVer 32 msiTransformValidationLess 64 msiTransformValidationLessOrEqual 128 msiTransformValidationEqual 256 msiTransformValidationGreaterOrEqual 512 msiTransformValidationGreater 1024 msiTransformValidationUpgradeCode 2048}

    # Enum MsiDoActionStatus
    array set MsiDoActionStatus {msiDoActionStatusNoAction 0 msiDoActionStatusSuccess 1 msiDoActionStatusUserExit 2 msiDoActionStatusFailure 3 msiDoActionStatusSuspend 4 msiDoActionStatusFinished 5 msiDoActionStatusWrongState 6 msiDoActionStatusBadActionData 7}

    # Enum MsiMessageStatus
    array set MsiMessageStatus {msiMessageStatusError -1 msiMessageStatusNone 0 msiMessageStatusOk 1 msiMessageStatusCancel 2 msiMessageStatusAbort 3 msiMessageStatusRetry 4 msiMessageStatusIgnore 5 msiMessageStatusYes 6 msiMessageStatusNo 7}

    # Enum MsiMessageType
    array set MsiMessageType {msiMessageTypeFatalExit 0 msiMessageTypeError 16777216 msiMessageTypeWarning 33554432 msiMessageTypeUser 50331648 msiMessageTypeInfo 67108864 msiMessageTypeFilesInUse 83886080 msiMessageTypeResolveSource 100663296 msiMessageTypeOutOfDiskSpace 117440512 msiMessageTypeActionStart 134217728 msiMessageTypeActionData 150994944 msiMessageTypeProgress 167772160 msiMessageTypeCommonData 184549376 msiMessageTypeOk 0 msiMessageTypeOkCancel 1 msiMessageTypeAbortRetryIgnore 2 msiMessageTypeYesNoCancel 3 msiMessageTypeYesNo 4 msiMessageTypeRetryCancel 5 msiMessageTypeDefault1 0 msiMessageTypeDefault2 256 msiMessageTypeDefault3 512}

    # Enum MsiInstallState
    array set MsiInstallState {msiInstallStateNotUsed -7 msiInstallStateBadConfig -6 msiInstallStateIncomplete -5 msiInstallStateSourceAbsent -4 msiInstallStateInvalidArg -2 msiInstallStateUnknown -1 msiInstallStateBroken 0 msiInstallStateAdvertised 1 msiInstallStateRemoved 1 msiInstallStateAbsent 2 msiInstallStateLocal 3 msiInstallStateSource 4 msiInstallStateDefault 5}

    # Enum MsiCostTree
    array set MsiCostTree {msiCostTreeSelfOnly 0 msiCostTreeChildren 1 msiCostTreeParents 2}

    # Enum MsiReinstallMode
    array set MsiReinstallMode {msiReinstallModeFileMissing 2 msiReinstallModeFileOlderVersion 4 msiReinstallModeFileEqualVersion 8 msiReinstallModeFileExact 16 msiReinstallModeFileVerify 32 msiReinstallModeFileReplace 64 msiReinstallModeMachineData 128 msiReinstallModeUserData 256 msiReinstallModeShortcut 512 msiReinstallModePackage 1024}

    # Enum MsiInstallType
    array set MsiInstallType {msiInstallTypeDefault 0 msiInstallTypeNetworkImage 1 msiInstallTypeSingleInstance 2}

    # Enum MsiInstallMode
    array set MsiInstallMode {msiInstallModeNoSourceResolution -3 msiInstallModeNoDetection -2 msiInstallModeExisting -1 msiInstallModeDefault 0}

    # Enum MsiSignatureInfo
    array set MsiSignatureInfo {msiSignatureInfoCertificate 0 msiSignatureInfoHash 1}

    # Enum MsiInstallContext
    array set MsiInstallContext {msiInstallContextFirstVisible 0 msiInstallContextUserManaged 1 msiInstallContextUser 2 msiInstallContextMachine 4 msiInstallContextAllUserManaged 8}

    # Enum MsiInstallSourceType
    array set MsiInstallSourceType {msiInstallSourceTypeUnknown 0 msiInstallSourceTypeNetwork 1 msiInstallSourceTypeURL 2 msiInstallSourceTypeMedia 4}

    # Enum Constants
    array set Constants {msiDatabaseNullInteger -2147483648}

    # Enum MsiOpenDatabaseMode
    array set MsiOpenDatabaseMode {msiOpenDatabaseModeReadOnly 0 msiOpenDatabaseModeTransact 1 msiOpenDatabaseModeDirect 2 msiOpenDatabaseModeCreate 3 msiOpenDatabaseModeCreateDirect 4 msiOpenDatabaseModePatchFile 32}

    # Enum MsiSignatureOption
    array set MsiSignatureOption {msiSignatureOptionInvalidHashFatal 1}
}

# Get the MSI installer
proc twapi::new_msi {} {
    return [comobj WindowsInstaller.Installer]
}

# Get the MSI installer
proc twapi::delete_msi {obj} {
    $obj -destroy
}

# Loads msi prototypes for the object
proc twapi::load_msi_prototypes {obj type} {

    # Init protos and stuff
    init_msi

    # Redefine ourselves so we don't call init_msi everytime
    proc ::twapi::load_msi_prototypes {obj type} {
        set type [string tolower $type]
        variable msi_guids

        # Tell the object it's type (guid)
        $obj -interfaceguid $msi_guids($type)
    }

    # Call our new definition
    return [load_msi_prototypes $obj $type]
}

