#import <Foundation/Foundation.h>
#include "core/preset.h"
/* JSON bytes preserve the shared schema across the process boundary. Both ends
 * parse/resolve independently; a client cannot bypass the provider's validation. */
@protocol ClumsierControl
- (void)configure:(NSData *)json baseline:(NSUInteger)baseline step:(NSUInteger)step start:(BOOL)start reply:(void (^)(NSString *error, BOOL running))reply;
- (void)stop:(void (^)(NSString *error, BOOL running))reply;
- (void)heartbeat:(void (^)(BOOL running))reply;
@end
static inline NSString *macPeerRequirement(NSString *bundle) {
    NSString *team = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"ClumsierTeam"];
    return [NSString stringWithFormat:@"anchor apple generic and identifier \"%@\" and certificate leaf[subject.OU] = \"%@\"", bundle, team];
}
