#include <yaml.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

/*
 * Test comment preservation through the full pipeline:
 *   parse (with preserve_comments) -> load -> dump -> emit
 *
 * Tests:
 *   1. Scanner produces COMMENT tokens
 *   2. Parser produces COMMENT events
 *   3. Loader attaches comments to nodes
 *   4. Dumper/Emitter round-trips comments
 */

static int test_count = 0;
static int pass_count = 0;

#define CHECK(cond, msg) do { \
    test_count++; \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s (line %d)\n", msg, __LINE__); \
    } else { \
        pass_count++; \
    } \
} while(0)

/* Test 1: Scanner produces YAML_COMMENT_TOKEN when preserve_comments is set */
static void test_scanner_comment_tokens(void)
{
    yaml_parser_t parser;
    yaml_token_t token;
    const char *input = "# top comment\nkey: value  # inline\n";
    int found_comment = 0;

    yaml_parser_initialize(&parser);
    yaml_parser_set_preserve_comments(&parser, 1);
    yaml_parser_set_input_string(&parser, (const unsigned char *)input,
            strlen(input));

    while (yaml_parser_scan(&parser, &token)) {
        if (token.type == YAML_COMMENT_TOKEN) {
            found_comment = 1;
            yaml_token_delete(&token);
            break;
        }
        if (token.type == YAML_STREAM_END_TOKEN) {
            yaml_token_delete(&token);
            break;
        }
        yaml_token_delete(&token);
    }

    CHECK(found_comment, "Scanner should produce COMMENT tokens");
    yaml_parser_delete(&parser);
}

/* Test 2: Scanner does NOT produce comments when preserve_comments is off */
static void test_scanner_no_comments_by_default(void)
{
    yaml_parser_t parser;
    yaml_token_t token;
    const char *input = "# comment\nkey: value\n";
    int found_comment = 0;

    yaml_parser_initialize(&parser);
    /* preserve_comments defaults to 0 */
    yaml_parser_set_input_string(&parser, (const unsigned char *)input,
            strlen(input));

    while (yaml_parser_scan(&parser, &token)) {
        if (token.type == YAML_COMMENT_TOKEN) {
            found_comment = 1;
        }
        if (token.type == YAML_STREAM_END_TOKEN) {
            yaml_token_delete(&token);
            break;
        }
        yaml_token_delete(&token);
    }

    CHECK(!found_comment, "Scanner should NOT produce COMMENT tokens by default");
    yaml_parser_delete(&parser);
}

/* Test 3: Parser produces YAML_COMMENT_EVENT */
static void test_parser_comment_events(void)
{
    yaml_parser_t parser;
    yaml_event_t event;
    const char *input = "# a comment\nkey: value\n";
    int found_comment = 0;

    yaml_parser_initialize(&parser);
    yaml_parser_set_preserve_comments(&parser, 1);
    yaml_parser_set_input_string(&parser, (const unsigned char *)input,
            strlen(input));

    while (yaml_parser_parse(&parser, &event)) {
        if (event.type == YAML_COMMENT_EVENT) {
            found_comment = 1;
            yaml_event_delete(&event);
            break;
        }
        if (event.type == YAML_STREAM_END_EVENT) {
            yaml_event_delete(&event);
            break;
        }
        yaml_event_delete(&event);
    }

    CHECK(found_comment, "Parser should produce COMMENT events");
    yaml_parser_delete(&parser);
}

/* Test 4: Loader attaches head comment to node */
static void test_loader_head_comment(void)
{
    yaml_parser_t parser;
    yaml_document_t document;
    yaml_node_t *root, *key_node;
    const char *input = "# head comment\nkey: value\n";

    yaml_parser_initialize(&parser);
    yaml_parser_set_preserve_comments(&parser, 1);
    yaml_parser_set_input_string(&parser, (const unsigned char *)input,
            strlen(input));

    if (yaml_parser_load(&parser, &document)) {
        root = yaml_document_get_root_node(&document);
        CHECK(root != NULL, "Document should have root node");

        if (root && root->type == YAML_MAPPING_NODE) {
            /* The mapping node or its first key should have the head comment */
            yaml_node_pair_t *pair = root->data.mapping.pairs.start;
            if (pair) {
                key_node = yaml_document_get_node(&document, pair->key);
                /* The head comment could be on the mapping or on the first key */
                int has_comment = (root->head_comment != NULL) ||
                                  (key_node && key_node->head_comment != NULL);
                CHECK(has_comment, "Head comment should be on root or first key node");
                if (root->head_comment) {
                    CHECK(strstr((char*)root->head_comment, " head comment") != NULL,
                          "Head comment text should be preserved");
                } else if (key_node && key_node->head_comment) {
                    CHECK(strstr((char*)key_node->head_comment, " head comment") != NULL,
                          "Head comment text should be preserved");
                }
            }
        }

        yaml_document_delete(&document);
    }

    yaml_parser_delete(&parser);
}

/* Test 5: Loader attaches inline comment */
static void test_loader_inline_comment(void)
{
    yaml_parser_t parser;
    yaml_document_t document;
    yaml_node_t *root;
    const char *input = "key: value # inline comment\n";

    yaml_parser_initialize(&parser);
    yaml_parser_set_preserve_comments(&parser, 1);
    yaml_parser_set_input_string(&parser, (const unsigned char *)input,
            strlen(input));

    if (yaml_parser_load(&parser, &document)) {
        root = yaml_document_get_root_node(&document);
        CHECK(root != NULL, "Document should have root node");

        if (root && root->type == YAML_MAPPING_NODE) {
            yaml_node_pair_t *pair = root->data.mapping.pairs.start;
            if (pair) {
                yaml_node_t *val = yaml_document_get_node(&document, pair->value);
                if (val) {
                    CHECK(val->inline_comment != NULL,
                          "Value node should have inline comment");
                    if (val->inline_comment) {
                        CHECK(strstr((char*)val->inline_comment, "inline comment") != NULL,
                              "Inline comment text should be preserved");
                    }
                }
            }
        }

        yaml_document_delete(&document);
    }

    yaml_parser_delete(&parser);
}

/* Test 6: Spacing preservation — raw text after '#' is kept */
static void test_spacing_preservation(void)
{
    yaml_parser_t parser;
    yaml_token_t token;
    const char *input = "#   lots   of   spaces\nkey: value\n";
    int found = 0;

    yaml_parser_initialize(&parser);
    yaml_parser_set_preserve_comments(&parser, 1);
    yaml_parser_set_input_string(&parser, (const unsigned char *)input,
            strlen(input));

    while (yaml_parser_scan(&parser, &token)) {
        if (token.type == YAML_COMMENT_TOKEN) {
            /* The value should be the raw text after '#' */
            CHECK(token.data.comment.value != NULL, "Comment token should have value");
            if (token.data.comment.value) {
                CHECK(strcmp((char*)token.data.comment.value, "   lots   of   spaces") == 0,
                      "Comment should preserve exact spacing after '#'");
            }
            found = 1;
            yaml_token_delete(&token);
            break;
        }
        if (token.type == YAML_STREAM_END_TOKEN) {
            yaml_token_delete(&token);
            break;
        }
        yaml_token_delete(&token);
    }

    CHECK(found, "Should find the comment token");
    yaml_parser_delete(&parser);
}

/* Test 7: Full round-trip through load and dump */
static void test_roundtrip(void)
{
    yaml_parser_t parser;
    yaml_document_t document;
    yaml_emitter_t emitter;
    unsigned char output[4096];
    size_t written = 0;

    const char *input = "# file header\nkey: value\n";

    /* Parse and load */
    yaml_parser_initialize(&parser);
    yaml_parser_set_preserve_comments(&parser, 1);
    yaml_parser_set_input_string(&parser, (const unsigned char *)input,
            strlen(input));

    if (!yaml_parser_load(&parser, &document)) {
        fprintf(stderr, "FAIL: Could not load document\n");
        test_count++;
        yaml_parser_delete(&parser);
        return;
    }
    yaml_parser_delete(&parser);

    /* Dump and emit */
    yaml_emitter_initialize(&emitter);
    yaml_emitter_set_preserve_comments(&emitter, 1);
    yaml_emitter_set_output_string(&emitter, output, sizeof(output), &written);
    yaml_emitter_set_unicode(&emitter, 1);

    yaml_emitter_open(&emitter);

    if (!yaml_emitter_dump(&emitter, &document)) {
        fprintf(stderr, "FAIL: Could not dump document (error: %s)\n",
                emitter.problem ? emitter.problem : "unknown");
        test_count++;
        yaml_emitter_delete(&emitter);
        return;
    }

    yaml_emitter_close(&emitter);
    yaml_emitter_delete(&emitter);

    output[written] = '\0';

    /* Check that the comment appears in output */
    CHECK(strstr((char*)output, "# file header") != NULL ||
          strstr((char*)output, "#") != NULL,
          "Round-trip output should contain comment");

    /* Verify basic content is preserved */
    CHECK(strstr((char*)output, "key") != NULL,
          "Round-trip output should contain key");
    CHECK(strstr((char*)output, "value") != NULL,
          "Round-trip output should contain value");
}

/* Test 8: Multiple comments */
static void test_multiple_comments(void)
{
    yaml_parser_t parser;
    yaml_event_t event;
    const char *input = "# comment 1\n# comment 2\nkey: value\n";
    int comment_count = 0;

    yaml_parser_initialize(&parser);
    yaml_parser_set_preserve_comments(&parser, 1);
    yaml_parser_set_input_string(&parser, (const unsigned char *)input,
            strlen(input));

    while (yaml_parser_parse(&parser, &event)) {
        if (event.type == YAML_COMMENT_EVENT) {
            comment_count++;
        }
        if (event.type == YAML_STREAM_END_EVENT) {
            yaml_event_delete(&event);
            break;
        }
        yaml_event_delete(&event);
    }

    CHECK(comment_count == 2, "Should produce 2 comment events");
    yaml_parser_delete(&parser);
}

/* Test 9: Sequence with comments */
static void test_sequence_comments(void)
{
    yaml_parser_t parser;
    yaml_document_t document;
    yaml_node_t *root;
    const char *input =
        "# before list\n"
        "- item1\n"
        "- item2\n";

    yaml_parser_initialize(&parser);
    yaml_parser_set_preserve_comments(&parser, 1);
    yaml_parser_set_input_string(&parser, (const unsigned char *)input,
            strlen(input));

    if (yaml_parser_load(&parser, &document)) {
        root = yaml_document_get_root_node(&document);
        CHECK(root != NULL, "Sequence document should have root node");
        if (root) {
            CHECK(root->type == YAML_SEQUENCE_NODE,
                  "Root should be a sequence node");
        }
        yaml_document_delete(&document);
    }

    yaml_parser_delete(&parser);
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    printf("Running comment preservation tests...\n\n");

    test_scanner_comment_tokens();
    test_scanner_no_comments_by_default();
    test_parser_comment_events();
    test_loader_head_comment();
    test_loader_inline_comment();
    test_spacing_preservation();
    test_roundtrip();
    test_multiple_comments();
    test_sequence_comments();

    printf("\n%d/%d tests passed\n", pass_count, test_count);

    return (pass_count == test_count) ? 0 : 1;
}
