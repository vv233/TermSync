// Verifies the platform CredentialStore round-trips a secret (store ->
// retrieve -> remove). On Windows this exercises the Credential Manager. Uses a
// unique test key and deletes it, so it leaves no residue. Exit 0 on success.

#include <QUuid>
#include <cstdio>

#include "credential/CredentialStore.h"

int main()
{
    auto store = termsync::core::CredentialStore::createDefault();
    std::fprintf(stderr, "[backend persistent=%d]\n", store->isPersistent());

    const QString key = "selftest-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString secret = "s3cr3t-é你好"; // includes non-ASCII

    if (!store->store(key, secret)) {
        std::fprintf(stderr, "store failed\n");
        return 1;
    }
    const QString got = store->retrieve(key);
    store->remove(key); // cleanup regardless of outcome

    if (got != secret) {
        std::fprintf(stderr, "mismatch: got '%s'\n", got.toUtf8().constData());
        return 1;
    }
    if (!store->retrieve(key).isEmpty()) {
        std::fprintf(stderr, "remove failed\n");
        return 1;
    }
    std::fprintf(stderr, "[ok] store/retrieve/remove round-trip verified\n");
    return 0;
}
