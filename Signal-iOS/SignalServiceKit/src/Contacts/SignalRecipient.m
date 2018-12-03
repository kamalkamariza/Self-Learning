//
//  Copyright (c) 2018 Open Whisper Systems. All rights reserved.
//

#import "SignalRecipient.h"
#import "OWSDevice.h"
#import "ProfileManagerProtocol.h"
#import "SSKEnvironment.h"
#import "TSAccountManager.h"
#import <SignalServiceKit/SignalServiceKit-Swift.h>
#import <YapDatabase/YapDatabaseConnection.h>

NS_ASSUME_NONNULL_BEGIN

@interface SignalRecipient ()

@property (nonatomic) NSOrderedSet *devices;

@end

#pragma mark -

@implementation SignalRecipient

#pragma mark - Dependencies

- (id<ProfileManagerProtocol>)profileManager
{
    return SSKEnvironment.shared.profileManager;
}

- (id<OWSUDManager>)udManager
{
    return SSKEnvironment.shared.udManager;
}

#pragma mark -

+ (instancetype)getOrBuildUnsavedRecipientForRecipientId:(NSString *)recipientId
                                             transaction:(YapDatabaseReadTransaction *)transaction
{
    OWSAssertDebug(transaction);
    OWSAssertDebug(recipientId.length > 0);

    SignalRecipient *_Nullable recipient =
        [self registeredRecipientForRecipientId:recipientId mustHaveDevices:NO transaction:transaction];
    if (!recipient) {
        recipient = [[self alloc] initWithTextSecureIdentifier:recipientId];
    }
    return recipient;
}

- (instancetype)initWithTextSecureIdentifier:(NSString *)textSecureIdentifier
{
    self = [super initWithUniqueId:textSecureIdentifier];
    if (!self) {
        return self;
    }

    OWSAssertDebug([TSAccountManager localNumber].length > 0);
    if ([[TSAccountManager localNumber] isEqualToString:textSecureIdentifier]) {
        // Default to no devices.
        //
        // This instance represents our own account and is used for sending
        // sync message to linked devices.  We shouldn't have any linked devices
        // yet when we create the "self" SignalRecipient, and we don't need to
        // send sync messages to the primary - we ARE the primary.
        _devices = [NSOrderedSet new];
    } else {
        // Default to sending to just primary device.
        //
        // OWSMessageSender will correct this if it is wrong the next time
        // we send a message to this recipient.
        _devices = [NSOrderedSet orderedSetWithObject:@(OWSDevicePrimaryDeviceId)];
    }

    return self;
}

- (nullable instancetype)initWithCoder:(NSCoder *)coder
{
    self = [super initWithCoder:coder];
    if (!self) {
        return self;
    }

    if (_devices == nil) {
        _devices = [NSOrderedSet new];
    }

    if ([self.uniqueId isEqual:[TSAccountManager localNumber]] &&
        [self.devices containsObject:@(OWSDevicePrimaryDeviceId)]) {
        OWSFailDebug(@"self as recipient device");
    }

    return self;
}

+ (nullable instancetype)registeredRecipientForRecipientId:(NSString *)recipientId
                                           mustHaveDevices:(BOOL)mustHaveDevices
                                               transaction:(YapDatabaseReadTransaction *)transaction
{
    OWSAssertDebug(transaction);
    OWSAssertDebug(recipientId.length > 0);

    SignalRecipient *_Nullable signalRecipient = [self fetchObjectWithUniqueID:recipientId transaction:transaction];
    if (mustHaveDevices && signalRecipient.devices.count < 1) {
        return nil;
    }
    return signalRecipient;
}

- (void)addDevices:(NSSet *)devices
{
    OWSAssertDebug(devices.count > 0);

    if ([self.uniqueId isEqual:[TSAccountManager localNumber]] &&
        [devices containsObject:@(OWSDevicePrimaryDeviceId)]) {
        OWSFailDebug(@"adding self as recipient device");
        return;
    }

    NSMutableOrderedSet *updatedDevices = [self.devices mutableCopy];
    [updatedDevices unionSet:devices];
    self.devices = [updatedDevices copy];
}

- (void)removeDevices:(NSSet *)devices
{
    OWSAssertDebug(devices.count > 0);

    NSMutableOrderedSet *updatedDevices = [self.devices mutableCopy];
    [updatedDevices minusSet:devices];
    self.devices = [updatedDevices copy];
}

- (void)updateRegisteredRecipientWithDevicesToAdd:(nullable NSArray *)devicesToAdd
                                  devicesToRemove:(nullable NSArray *)devicesToRemove
                                      transaction:(YapDatabaseReadWriteTransaction *)transaction {
    OWSAssertDebug(transaction);
    OWSAssertDebug(devicesToAdd.count > 0 || devicesToRemove.count > 0);

    // Add before we remove, since removeDevicesFromRecipient:...
    // can markRecipientAsUnregistered:... if the recipient has
    // no devices left.
    if (devicesToAdd.count > 0) {
        [self addDevicesToRegisteredRecipient:[NSSet setWithArray:devicesToAdd] transaction:transaction];
    }
    if (devicesToRemove.count > 0) {
        [self removeDevicesFromRecipient:[NSSet setWithArray:devicesToRemove] transaction:transaction];
    }

    // Device changes
    dispatch_async(dispatch_get_main_queue(), ^{
        // Device changes can affect the UD access mode for a recipient,
        // so we need to fetch the profile for this user to update UD access mode.
        [self.profileManager fetchProfileForRecipientId:self.recipientId];
    });
}

- (void)addDevicesToRegisteredRecipient:(NSSet *)devices transaction:(YapDatabaseReadWriteTransaction *)transaction
{
    OWSAssertDebug(transaction);
    OWSAssertDebug(devices.count > 0);
    OWSLogDebug(@"adding devices: %@, to recipient: %@", devices, self);

    [self reloadWithTransaction:transaction];
    [self addDevices:devices];
    [self saveWithTransaction_internal:transaction];
}

- (void)removeDevicesFromRecipient:(NSSet *)devices transaction:(YapDatabaseReadWriteTransaction *)transaction
{
    OWSAssertDebug(transaction);
    OWSAssertDebug(devices.count > 0);

    OWSLogDebug(@"removing devices: %@, from registered recipient: %@", devices, self);
    [self reloadWithTransaction:transaction];
    [self removeDevices:devices];
    [self saveWithTransaction_internal:transaction];
}

- (NSString *)recipientId
{
    return self.uniqueId;
}

- (NSComparisonResult)compare:(SignalRecipient *)other
{
    return [self.recipientId compare:other.recipientId];
}

- (void)removeWithTransaction:(YapDatabaseReadWriteTransaction *)transaction
{
    // We need to distinguish between "users we know to be unregistered" and
    // "users whose registration status is unknown".  The former correspond to
    // instances of SignalRecipient with no devices.  The latter do not
    // correspond to an instance of SignalRecipient in the database (although
    // they may correspond to an "unsaved" instance of SignalRecipient built
    // by getOrBuildUnsavedRecipientForRecipientId.
    OWSFailDebug(@"Don't call removeWithTransaction.");
    
    [super removeWithTransaction:transaction];
}

- (void)saveWithTransaction:(YapDatabaseReadWriteTransaction *)transaction
{
    // We only want to mutate the persisted SignalRecipients in the database
    // using other methods of this class, e.g. markRecipientAsRegistered...
    // to create, addDevices and removeDevices to mutate.  We're trying to
    // be strict about using persisted SignalRecipients as a cache to
    // reflect "last known registration status".  Forcing our codebase to
    // use those methods helps ensure that we update the cache deliberately.
    OWSFailDebug(@"Don't call saveWithTransaction from outside this class.");
    
    [self saveWithTransaction_internal:transaction];
}

- (void)saveWithTransaction_internal:(YapDatabaseReadWriteTransaction *)transaction
{
    [super saveWithTransaction:transaction];

    OWSLogVerbose(@"saved signal recipient: %@ (%lu)", self.recipientId, (unsigned long) self.devices.count);
}

+ (BOOL)isRegisteredRecipient:(NSString *)recipientId transaction:(YapDatabaseReadTransaction *)transaction
{
    return nil != [self registeredRecipientForRecipientId:recipientId mustHaveDevices:YES transaction:transaction];
}

+ (SignalRecipient *)markRecipientAsRegisteredAndGet:(NSString *)recipientId
                                         transaction:(YapDatabaseReadWriteTransaction *)transaction
{
    OWSAssertDebug(transaction);
    OWSAssertDebug(recipientId.length > 0);

    SignalRecipient *_Nullable instance =
        [self registeredRecipientForRecipientId:recipientId mustHaveDevices:YES transaction:transaction];

    if (!instance) {
        OWSLogDebug(@"creating recipient: %@", recipientId);

        instance = [[self alloc] initWithTextSecureIdentifier:recipientId];
        [instance saveWithTransaction_internal:transaction];
    }
    return instance;
}

+ (void)markRecipientAsRegistered:(NSString *)recipientId
                         deviceId:(UInt32)deviceId
                      transaction:(YapDatabaseReadWriteTransaction *)transaction
{
    OWSAssertDebug(transaction);
    OWSAssertDebug(recipientId.length > 0);

    SignalRecipient *recipient = [self markRecipientAsRegisteredAndGet:recipientId transaction:transaction];
    if (![recipient.devices containsObject:@(deviceId)]) {
        OWSLogDebug(@"Adding device %u to existing recipient.", (unsigned int)deviceId);

        [recipient addDevices:[NSSet setWithObject:@(deviceId)]];
        [recipient saveWithTransaction_internal:transaction];
    }
}

+ (void)markRecipientAsUnregistered:(NSString *)recipientId transaction:(YapDatabaseReadWriteTransaction *)transaction
{
    OWSAssertDebug(transaction);
    OWSAssertDebug(recipientId.length > 0);

    SignalRecipient *instance = [self getOrBuildUnsavedRecipientForRecipientId:recipientId
                                                                   transaction:transaction];
    OWSLogDebug(@"Marking recipient as not registered: %@", recipientId);
    if (instance.devices.count > 0) {
        [instance removeDevices:instance.devices.set];
    }
    [instance saveWithTransaction_internal:transaction];
}

@end

NS_ASSUME_NONNULL_END
