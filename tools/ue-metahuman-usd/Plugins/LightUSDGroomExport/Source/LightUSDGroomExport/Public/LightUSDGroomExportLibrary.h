#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "LightUSDGroomExportLibrary.generated.h"

class UGroomAsset;

UCLASS()
class LIGHTUSDGROOMEXPORT_API ULightUSDGroomExportLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /**
     * Writes the source HairDescription as a portable linear UsdGeomBasisCurves
     * prim.  MaxCurves retains evenly distributed authored strands, keeping
     * interactive raster previews practical without synthesising any hair.
     */
    UFUNCTION(BlueprintCallable, Category = "LightUSD|Groom")
    static bool ExportGroomToUsd(UGroomAsset* Groom, const FString& Filename,
                                 int32 MaxCurves = 15000);
};
