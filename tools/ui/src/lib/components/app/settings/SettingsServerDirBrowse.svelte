<script lang="ts">
	import { FolderPlus } from '@lucide/svelte';
	import { Button } from '$lib/components/ui/button';
	import * as Dialog from '$lib/components/ui/dialog';
	import { Input } from '$lib/components/ui/input';
	import { API_UPLOADS } from '$lib/constants';
	import { apiFetch } from '$lib/utils';

	interface DirEntry {
		name: string;
		path: string;
		writable: boolean;
	}

	interface DirList {
		entries: DirEntry[];
		parent: string;
		path: string;
		writable: boolean;
	}

	interface Props {
		onSelect: (path: string) => void;
		open?: boolean;
		startPath?: string;
	}

	let { onSelect, open = $bindable(false), startPath = '' }: Props = $props();

	let listing = $state<DirList | null>(null);
	let error = $state('');
	let newName = $state('');
	let loading = $state(false);

	async function load(path: string) {
		loading = true;
		error = '';

		try {
			const query = path ? `?path=${encodeURIComponent(path)}` : '';
			listing = await apiFetch<DirList>(`${API_UPLOADS.DIRS}${query}`);
		} catch (err) {
			error = err instanceof Error ? err.message : 'Failed to list folders';
			listing = null;
		} finally {
			loading = false;
		}
	}

	$effect(() => {
		if (open) {
			void load(startPath.trim());
		}
	});

	async function createFolder() {
		if (!listing?.path || !newName.trim()) {
			return;
		}

		error = '';

		try {
			const created = await apiFetch<{ path: string }>(API_UPLOADS.DIRS, {
				body: JSON.stringify({ name: newName.trim(), parent: listing.path }),
				method: 'POST'
			});

			newName = '';
			await load(listing.path);

			if (created.path) {
				await load(created.path);
			}
		} catch (err) {
			error = err instanceof Error ? err.message : 'Failed to create folder';
		}
	}
</script>

<Dialog.Root bind:open>
	<Dialog.Content class="sm:max-w-lg">
		<Dialog.Header>
			<Dialog.Title>Choose attachment folder</Dialog.Title>
			<Dialog.Description>
				Only directories the llama-server process can write are listed.
			</Dialog.Description>
		</Dialog.Header>

		{#if listing?.path}
			<p class="break-all font-mono text-xs text-muted-foreground">{listing.path}</p>
		{:else}
			<p class="text-xs text-muted-foreground">Writable roots</p>
		{/if}

		{#if error}
			<p class="text-sm text-destructive">{error}</p>
		{/if}

		<div class="max-h-64 overflow-y-auto rounded-md border">
			{#if loading}
				<p class="p-3 text-sm text-muted-foreground">Loading…</p>
			{:else if listing}
				{#if listing.parent}
					<button
						class="block w-full px-3 py-2 text-left text-sm hover:bg-accent"
						onclick={() => load(listing.parent)}
						type="button"
					>
						..
					</button>
				{:else if listing.path}
					<button
						class="block w-full px-3 py-2 text-left text-sm hover:bg-accent"
						onclick={() => load('')}
						type="button"
					>
						..
					</button>
				{/if}

				{#each listing.entries as entry (entry.path)}
					<button
						class="block w-full px-3 py-2 text-left text-sm hover:bg-accent"
						onclick={() => load(entry.path)}
						type="button"
					>
						{entry.name}
					</button>
				{/each}

				{#if listing.entries.length === 0 && listing.path}
					<p class="p-3 text-sm text-muted-foreground">No writable subfolders</p>
				{/if}
			{/if}
		</div>

		{#if listing?.writable && listing.path}
			<div class="flex gap-2">
				<Input
					class="flex-1"
					onkeydown={(event) => {
						if (event.key === 'Enter') {
							event.preventDefault();
							void createFolder();
						}
					}}
					placeholder="New subfolder name"
					bind:value={newName}
				/>
				<Button onclick={() => createFolder()} type="button" variant="secondary">
					<FolderPlus class="h-4 w-4" />
					Create
				</Button>
			</div>
		{/if}

		<Dialog.Footer>
			<Button onclick={() => (open = false)} type="button" variant="ghost">Cancel</Button>
			<Button
				disabled={!listing?.writable || !listing.path}
				onclick={() => {
					if (listing?.path) {
						onSelect(listing.path);
						open = false;
					}
				}}
				type="button"
			>
				Use this folder
			</Button>
		</Dialog.Footer>
	</Dialog.Content>
</Dialog.Root>
