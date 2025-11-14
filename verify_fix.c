#include <stdio.h>
#include <string.h>

// Test function from storage_server.c word indexing logic
void split_into_words(const char* sentence, char words[][256], int* count) {
    *count = 0;
    int j = 0;
    for (int i = 0; sentence[i]; i++) {
        if (sentence[i] == ' ') {
            if (j > 0 && *count < 100) {
                words[*count][j] = '\0';
                (*count)++;
                j = 0;
            }
        } else {
            if (j < 255) {
                words[*count][j++] = sentence[i];
            }
        }
    }
    if (j > 0 && *count < 100) {
        words[*count][j] = '\0';
        (*count)++;
    }
}

int main() {
    printf("Testing Word Indexing Fix...\n");
    
    // Test scenario: sentence "Hello World" 
    // User wants to insert "Beautiful" at position 2 (1-based indexing)
    // Expected result: "Hello Beautiful World"
    
    char sentence[] = "Hello World";
    char words[100][256];
    int word_count = 0;
    
    split_into_words(sentence, words, &word_count);
    
    printf("Original sentence: '%s'\n", sentence);
    printf("Word count: %d\n", word_count);
    printf("Words:\n");
    for (int i = 0; i < word_count; i++) {
        printf("  [%d]: '%s'\n", i + 1, words[i]); // Show 1-based indexing
    }
    
    // Test our 1-based indexing logic
    int word_idx = 2; // User wants to insert at position 2 (1-based)
    char content[] = "Beautiful";
    
    // OLD LOGIC (0-based) - WRONG:
    // if (word_idx < 0 || word_idx > word_count) - would fail for word_idx=2
    
    // NEW LOGIC (1-based) - CORRECT:
    if (word_idx < 1 || word_idx > word_count + 1) {
        printf("ERROR: Word index out of range (1-based indexing)\n");
        return 1;
    }
    
    // Build new sentence with 1-based indexing
    char new_sentence[1024] = "";
    
    // Insert words before the target position (0 to word_idx-2)
    for (int i = 0; i < word_idx - 1; i++) {
        if (strlen(new_sentence) > 0) strcat(new_sentence, " ");
        strcat(new_sentence, words[i]);
    }
    
    // Insert the new content
    if (strlen(new_sentence) > 0) strcat(new_sentence, " ");
    strcat(new_sentence, content);
    
    // Insert remaining words (word_idx-1 to word_count-1)
    for (int i = word_idx - 1; i < word_count; i++) {
        strcat(new_sentence, " ");
        strcat(new_sentence, words[i]);
    }
    
    printf("\nAfter inserting '%s' at position %d:\n", content, word_idx);
    printf("Result: '%s'\n", new_sentence);
    printf("Expected: 'Hello Beautiful World'\n");
    
    if (strcmp(new_sentence, "Hello Beautiful World") == 0) {
        printf("✅ Word indexing fix is CORRECT!\n");
        return 0;
    } else {
        printf("❌ Word indexing fix is WRONG!\n");
        return 1;
    }
}