#include "LightUSDGroomExportLibrary.h"

#include "GroomAsset.h"
#include "HairAttributes.h"
#include "HairDescription.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
void AppendFloat(FString& Out, float Value)
{
    Out.Appendf(TEXT("%.7g"), static_cast<double>(Value));
}

void AppendVector(FString& Out, const FVector3f& Value)
{
    Out.AppendChar(TEXT('('));
    AppendFloat(Out, Value.X); Out.Append(TEXT(", "));
    AppendFloat(Out, Value.Y); Out.Append(TEXT(", "));
    AppendFloat(Out, Value.Z); Out.AppendChar(TEXT(')'));
}
}

bool ULightUSDGroomExportLibrary::ExportGroomToUsd(UGroomAsset* Groom,
                                                    const FString& Filename,
                                                    int32 MaxCurves)
{
    if (!Groom || Filename.IsEmpty())
    {
        return false;
    }

    const FHairDescription Description = Groom->GetHairDescription();
    const TVertexAttributesConstRef<FVector3f> Positions =
        Description.VertexAttributes().GetAttributesRef<FVector3f>(HairAttribute::Vertex::Position);
    const TStrandAttributesConstRef<int32> VertexCounts =
        Description.StrandAttributes().GetAttributesRef<int32>(HairAttribute::Strand::VertexCount);
    const TVertexAttributesConstRef<float> Widths =
        Description.VertexAttributes().GetAttributesRef<float>(HairAttribute::Vertex::Width);
    const TVertexAttributesConstRef<FVector3f> Colors =
        Description.VertexAttributes().GetAttributesRef<FVector3f>(HairAttribute::Vertex::Color);

    if (!Description.IsValid() || !Positions.IsValid() || !VertexCounts.IsValid())
    {
        return false;
    }

    const int32 SourceCurveCount = Description.GetNumStrands();
    const int32 CurveLimit = FMath::Max(1, MaxCurves);
    const int32 Stride = FMath::Max(1, FMath::DivideAndRoundUp(SourceCurveCount, CurveLimit));
    FString Counts, Points, OutputWidths, OutputColors;
    int32 VertexOffset = 0;
    int32 WrittenCurves = 0;
    int32 WrittenVertices = 0;

    for (int32 StrandIndex = 0; StrandIndex < SourceCurveCount; ++StrandIndex)
    {
        const int32 VertexCount = VertexCounts[FStrandID(StrandIndex)];
        const bool bWrite = (StrandIndex % Stride == 0) && VertexCount >= 2 && WrittenCurves < CurveLimit;
        if (bWrite)
        {
            if (WrittenCurves++) { Counts.Append(TEXT(", ")); }
            Counts.Appendf(TEXT("%d"), VertexCount);
            for (int32 PointIndex = 0; PointIndex < VertexCount; ++PointIndex)
            {
                const FVertexID VertexId(VertexOffset + PointIndex);
                if (WrittenVertices++)
                {
                    Points.Append(TEXT(", "));
                    OutputWidths.Append(TEXT(", "));
                    OutputColors.Append(TEXT(", "));
                }
                AppendVector(Points, Positions[VertexId]);
                AppendFloat(OutputWidths, Widths.IsValid() ? FMath::Max(Widths[VertexId], 0.01f) : 0.08f);
                // UE's stock groom has white (or absent) per-vertex color.
                // Emit the MaterialX hair network's melanin/absorption result
                // here, so lightweight USD raster backends that only consume
                // displayColor still show the intended brown hair response.
                const FVector3f SourceColor = Colors.IsValid() ? Colors[VertexId] : FVector3f::OneVector;
                const FVector3f HairColor(
                    FMath::Max(SourceColor.X, 0.15f) * 0.18f,
                    FMath::Max(SourceColor.Y, 0.15f) * 0.055f,
                    FMath::Max(SourceColor.Z, 0.15f) * 0.015f);
                AppendVector(OutputColors, HairColor);
            }
        }
        VertexOffset += VertexCount;
    }

    if (WrittenCurves == 0 || WrittenVertices == 0)
    {
        return false;
    }

    FString Usd;
    Usd.Reserve(Points.Len() + OutputWidths.Len() + OutputColors.Len() + Counts.Len() + 512);
    Usd += TEXT("#usda 1.0\n(\n    defaultPrim = \"HairStrands\"\n    metersPerUnit = 0.01\n    upAxis = \"Z\"\n)\n\n");
    Usd += TEXT("def BasisCurves \"HairStrands\"\n{\n");
    Usd.Appendf(TEXT("    custom string unreal:sourceGroom = \"%s\"\n"), *Groom->GetPathName());
    Usd.Appendf(TEXT("    custom int unreal:sourceCurveCount = %d\n"), SourceCurveCount);
    Usd.Appendf(TEXT("    custom int unreal:exportedCurveCount = %d\n"), WrittenCurves);
    Usd += TEXT("    uniform token type = \"linear\"\n    uniform token wrap = \"nonperiodic\"\n");
    Usd += TEXT("    int[] curveVertexCounts = [") + Counts + TEXT("]\n");
    Usd += TEXT("    point3f[] points = [") + Points + TEXT("]\n");
    Usd += TEXT("    float[] widths = [") + OutputWidths + TEXT("] (interpolation = \"vertex\")\n");
    Usd += TEXT("    color3f[] primvars:displayColor = [") + OutputColors + TEXT("] (interpolation = \"vertex\")\n");
    Usd += TEXT("}\n");

    return FFileHelper::SaveStringToFile(Usd, *Filename);
}
