#!/bin/bash
set -euo pipefail
# Remove everything deploy.sh created. Log groups are deleted too -- Lambda
# creates them implicitly, so they outlive the function otherwise.
FN="${FN:-prp-bench}"
ROLE="${ROLE:-${FN}-role}"
REGION="${REGION:-$(aws configure get region 2>/dev/null || echo us-east-1)}"

aws lambda delete-function --function-name "$FN" --region "$REGION" 2>/dev/null && echo "deleted function $FN" || true
aws logs delete-log-group --log-group-name "/aws/lambda/$FN" --region "$REGION" 2>/dev/null && echo "deleted log group" || true
aws iam detach-role-policy --role-name "$ROLE" \
  --policy-arn arn:aws:iam::aws:policy/service-role/AWSLambdaBasicExecutionRole 2>/dev/null || true
aws iam delete-role --role-name "$ROLE" 2>/dev/null && echo "deleted role $ROLE" || true
echo "teardown complete"
