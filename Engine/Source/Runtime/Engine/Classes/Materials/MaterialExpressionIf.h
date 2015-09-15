// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.


#pragma once
#include "Materials/MaterialExpression.h"
#include "MaterialExpressionIf.generated.h"

UCLASS(collapsecategories, hidecategories=Object, MinimalAPI)
class UMaterialExpressionIf : public UMaterialExpression
{
	GENERATED_UCLASS_BODY()

	UPROPERTY()
	FExpressionInput A;

	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'ConstB' if not specified"))
	FExpressionInput B;

	UPROPERTY()
	FExpressionInput AGreaterThanB;

	UPROPERTY(meta = (RequiredInput = "false"))
	FExpressionInput AEqualsB;

	UPROPERTY()
	FExpressionInput ALessThanB;

	UPROPERTY(EditAnywhere, Category=MaterialExpressionIf)
	float EqualsThreshold;

	/** only used if B is not hooked up */
	UPROPERTY(EditAnywhere, Category = MaterialExpressionIf)
	float ConstB;

	UPROPERTY()
	float ConstAEqualsB_DEPRECATED;

	//~ Begin UMaterialExpression Interface
	virtual int32 Compile(class FMaterialCompiler* Compiler, int32 OutputIndex, int32 MultiplexIndex) override;
	virtual void GetCaption(TArray<FString>& OutCaptions) const override;
#if WITH_EDITOR
	virtual uint32 GetInputType(int32 InputIndex) override;
	virtual uint32 GetOutputType(int32 InputIndex) override {return MCT_Unknown;}
#endif
	//~ End UMaterialExpression Interface
};



