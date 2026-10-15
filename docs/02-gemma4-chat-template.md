# Gemma 4 chat template (llama.cpp)

Canonical Jinja: [`templates/google-gemma-4-canonical.jinja`](../templates/google-gemma-4-canonical.jinja).

Pass to llama.cpp:

```bash
llama-server ... --jinja --chat-template-file /path/to/google-gemma-4-canonical.jinja
```

Live stack (`stack2-llama-chat`) bind-mounts this file (or the identical copy under
`local-agent/templates/`) as `/templates/gemma4.jinja`.

## Contract that Arli must satisfy

The template **raises** if `tool_calls[].function.arguments` is a JSON string
(OpenAI Chat Completions wire format). Arguments must be a **JSON object**
before the request is applied through the template.

Arli does this in `chat_completions` (`messages_for_gemma_template`): string →
parsed object on the outbound payload only; in-memory tool dispatch still uses
the string form from the model reply.

Also: `tool_choice` is never `required` (sampler HTTP 400 on Gemma4);
`parallel_tool_calls=false`.

## Thinking must stay ON (agent / tools)

Google ([Thinking mode](https://ai.google.dev/gemma/docs/capabilities/thinking)):

- For **12B / 26B / 31B**, `enable_thinking=false` makes the template prefill a
  **closed empty** thought block (`<|channel>thought\n<channel|>`) — thinking OFF.
- With tools: *«If a single model turn involves function or tool calls, thoughts
  must NOT be removed between the function calls.»*

llama.cpp (`--reasoning auto`, Gemma4 parser / interleaved template,
[PR #21418](https://github.com/ggml-org/llama.cpp/pull/21418)): after a tool
response the template must open `<|channel>thought\n` again — only when
`enable_thinking` is true.

Arli always sends:

```json
"chat_template_kwargs": {"enable_thinking": true, "preserve_thinking": true}
```

Do **not** pass `enable_thinking: false` and do **not** strip
`reasoning` / `reasoning_content` from assistant messages that still have
pending tool rounds in the same turn.

## Agent sampling (0.4.1+)

Gemma 4 requests use Google's recommended temperature=1.0, top_p=0.95, top_k=64; min_p=0, repeat_penalty=1, presence_penalty=0. A production request reserves one quarter of context_tokens for its response, capped at 8192 for Gemma (including reasoning). The new default context window 16384 reserves 4096. reasoning_budget_tokens is min(2048, response reserve / 2); the direct legacy LLM API without a configured window retains max_tokens=8192. Use reasoning_budget_tokens (normally 2048) (llama.cpp build 11429 schema), not the old thinking_budget_tokens=384 payload. The compose server must use --reasoning-budget 2048 too.

For the RTX 3090 stack2 configuration: server context 64000, batch 2048, microbatch 512, f16 KV, parallel=1. Arli's context window is 49152 to leave headroom for tool declarations and generation; max_steps=64 bounds a stuck turn. A smaller 32768 server context can use a 24576 client window. The client setting must be changed through Settings/RPC; changing compose alone does not change Arli's stored window.

Sources: [Gemma model card](https://ai.google.dev/gemma/docs/core/model_card_4#best-practices), [running llama.cpp revision schema](https://github.com/ggml-org/llama.cpp/blob/d81235049/tools/server/server-schema.cpp).

## Structured action output (0.4.6+)

For tool-enabled Gemma requests, Arli sends response_format=json_schema with a closed
choice of final answer or a catalog tool and its required parameters. llama.cpp enforces
this during generation. The client converts the validated result into the ordinary
assistant tool_calls object before dispatch and before the next history request.
Native reasoning and canonical tool-result history remain enabled. The application
does not execute textual tool tags. This addresses missing parameters and malformed
native Gemma dictionary keys observed in the longer real-project acceptance test.
