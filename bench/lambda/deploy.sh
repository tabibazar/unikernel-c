#!/bin/bash
set -euo pipefail

# Create (or update) the Lambda used for the substrate comparison.
#
# Creates exactly two things in the account, both named with the same prefix
# so teardown.sh can find them: one IAM role with the AWS-managed basic
# execution policy, and one function. Nothing else, no VPC, no log retention
# changes. teardown.sh removes both.

FN="${FN:-prp-bench}"
ROLE="${ROLE:-${FN}-role}"
REGION="${REGION:-$(aws configure get region 2>/dev/null || echo us-east-1)}"
MEM="${MEM:-1769}"          # 1769 MB is where Lambda gives a full vCPU
TIMEOUT="${TIMEOUT:-300}"
REPS="${REPS:-10}"
PRIMORIAL="${PRIMORIAL:-2357}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="$SCRIPT_DIR/build"
[ -x "$BUILD/prp_bench" ] || { echo "run build.sh first" >&2; exit 1; }

cp "$SCRIPT_DIR/bootstrap" "$BUILD/bootstrap"
chmod +x "$BUILD/bootstrap"
( cd "$BUILD" && rm -f fn.zip && zip -q fn.zip bootstrap prp_bench )
echo "package: $(du -h "$BUILD/fn.zip" | cut -f1)"

if ! aws iam get-role --role-name "$ROLE" >/dev/null 2>&1; then
	echo "creating role $ROLE"
	aws iam create-role --role-name "$ROLE" \
	  --assume-role-policy-document '{"Version":"2012-10-17","Statement":[{"Effect":"Allow","Principal":{"Service":"lambda.amazonaws.com"},"Action":"sts:AssumeRole"}]}' \
	  >/dev/null
	aws iam attach-role-policy --role-name "$ROLE" \
	  --policy-arn arn:aws:iam::aws:policy/service-role/AWSLambdaBasicExecutionRole
	# IAM is eventually consistent: a role can exist and still be rejected by
	# Lambda for several seconds after creation.
	echo "waiting for the role to propagate"
	sleep 12
fi
ROLE_ARN=$(aws iam get-role --role-name "$ROLE" --query Role.Arn --output text)

ENV="Variables={PRP_REPS=$REPS,PRP_PRIMORIAL=$PRIMORIAL}"

if aws lambda get-function --function-name "$FN" --region "$REGION" >/dev/null 2>&1; then
	echo "updating $FN"
	aws lambda update-function-code --function-name "$FN" --region "$REGION" \
	  --zip-file "fileb://$BUILD/fn.zip" >/dev/null
	aws lambda wait function-updated --function-name "$FN" --region "$REGION"
	aws lambda update-function-configuration --function-name "$FN" --region "$REGION" \
	  --memory-size "$MEM" --timeout "$TIMEOUT" --environment "$ENV" >/dev/null
else
	echo "creating $FN at ${MEM} MB"
	aws lambda create-function --function-name "$FN" --region "$REGION" \
	  --runtime provided.al2023 --architectures x86_64 --handler bootstrap \
	  --role "$ROLE_ARN" --zip-file "fileb://$BUILD/fn.zip" \
	  --memory-size "$MEM" --timeout "$TIMEOUT" --environment "$ENV" >/dev/null
fi
aws lambda wait function-updated --function-name "$FN" --region "$REGION"
echo "ready: $FN in $REGION at ${MEM} MB"
